#ifndef DIANCIE_RPC_LIB_HPP
#define DIANCIE_RPC_LIB_HPP

#include "../includes/a_cxl_connector.hpp"
#include "../includes/cxl_switch_ipc.h"
#include "../includes/ioctl_defs.h"
#include "../includes/qemu_cxl_connector.hpp"
#include "../includes/rpc_interface.hpp"
#include "../includes/mmio.hpp"
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <signal.h>
#include <stddef.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace diancie {

// Handle thread join segfault error without slowing down code path with unnecc
// checking for closure at every mem access
class SegfaultException : public std::runtime_error {
public:
  SegfaultException()
      : std::runtime_error("SegfaultException") {}
};

static void segfault_handler(int sig) {
  throw SegfaultException();
}

using ClientId = std::string;
using ChannelId = uint64_t;

template <typename FunctionEnum>
class DiancieServer : protected QEMUCXLConnector {
public:
  // Constructors
  DiancieServer() = delete;

  DiancieServer(const std::string &device_path, const std::string &service_name,
                const std::string &instance_id)
      : QEMUCXLConnector(device_path), service_name_(service_name),
        instance_id_(instance_id) {
    std::cout << "DiancieServer initialized" << std::endl;
  }

  ~DiancieServer() {
    std::cout << "Cleaning up Diancie server resources." << std::endl;
    for (auto &[channel_id, thread] : clients_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
    clients_.clear();
    deregister_service();
    std::cout << "Diancie server resources cleaned up." << std::endl;
  }

  DiancieServer(const DiancieServer &) = delete;
  DiancieServer &operator=(const DiancieServer &) = delete;

  DiancieServer(DiancieServer &&) = delete;
  DiancieServer &operator=(DiancieServer &&) = delete;

  template <FunctionEnum func_id, typename Handler>
  void register_rpc_function(Handler &&handler) {
    using Traits = DiancieFunctionTraits<FunctionEnum, func_id>;
    using RetType = typename Traits::ReturnType;
    using ArgsTuple = typename Traits::ArgsTuple;

    auto wrapper = [handler = std::move(handler)](void *args_region,
                                                  void *result) {
      try {
        ArgsTuple *args = reinterpret_cast<ArgsTuple *>(args_region);
        std::cout << "Address of args is at " << &args << std::endl;
        if constexpr (std::is_void_v<RetType>) {
          std::apply(handler, *args);
        } else {
          RetType *result_ptr = reinterpret_cast<RetType *>(result);
          *result_ptr = std::apply(handler, *args);
          // RetType result_value = std::apply(handler, *args);
          // mmio_write(result_ptr, result_value);
          std::cout << "Server has written result to result ptr" << std::endl;
        }
      } catch (const std::exception &e) {
        std::cerr << "Error in " << Traits::name << ": " << e.what()
                  << std::endl;
        throw;
      }
    };

    constexpr size_t args_size = sizeof(ArgsTuple);
    constexpr size_t result_size =
        std::is_void_v<RetType> ? 0 : sizeof(RetType);

    function_registry_[func_id] =
        FunctionInfo{.handler = wrapper,
                     .args_size = args_size,
                     .result_size = result_size,
                     .name = std::string(Traits::name)};

    std::cout << "Registered " << Traits::name
              << " (ID: " << static_cast<uint32_t>(func_id)
              << ", Args: " << args_size << ", Result: " << result_size << ")"
              << std::endl;
  }

  void run_server_loop() {
    bool running = true;
    while (running) {
      try {
        auto event_data = wait_for_event(1000);

        if (!event_data) {
          continue;
        }

        switch (event_data->type) {
        case CXLEvent::NEW_CLIENT_CONNECTED:
          std::cout << "Received New client connected event from event loop"
                    << std::endl;
          handle_new_client(std::move(event_data->connection));
          break;
        case CXLEvent::CHANNEL_CLOSED:
          std::cout << "Received Channel closed event from event loop"
                    << std::endl;
          handle_channel_close(event_data->channel_id);
          break;
        case CXLEvent::CLIENT_DISCONNECTED:
          std::cout << "Received Client disconnected event from event loop"
                    << std::endl;
          handle_client_disconnect(event_data->channel_id);
          break;
        case CXLEvent::COMMAND_RECEIVED:
          break;
        case CXLEvent::ERROR_OCURRED:
          break;
        default:
          std::cout << "DiancieServer: Unknown event type" << std::endl;
          break;
        }
      } catch (const std::exception &e) {
        std::cerr << "DiancieServer: " << e.what() << std::endl;
      }
    }
  }

  bool register_service() {
    if (has_registered_service) {
      throw std::runtime_error("Cannot register service twice!");
    }
    cxl_ipc_rpc_register_service_req_t req;
    std::memset(&req, 0, sizeof(req));
    req.type = CXL_MSG_TYPE_RPC_REGISTER_SERVICE_REQ;

    strncpy(req.service_name, service_name_.c_str(), MAX_SERVICE_NAME_LEN - 1);
    req.service_name[MAX_SERVICE_NAME_LEN - 1] = '\0';

    strncpy(req.instance_id, instance_id_.c_str(), MAX_INSTANCE_ID_LEN - 1);
    req.instance_id[MAX_INSTANCE_ID_LEN - 1] = '\0';

    std::cout << "DiancieServer: Preparing to register service '"
              << req.service_name << "' with instance ID '" << req.instance_id
              << "'" << std::endl;

    if (send_command(&req, sizeof(req))) {
      cxl_ipc_rpc_register_service_resp_t resp;
      recv_response(&resp, sizeof(resp));
      if (resp.status == CXL_IPC_STATUS_OK) {
        has_registered_service = true;
        return true;
      }
    }
    return false;
  }

private:
  std::string service_name_;
  std::string instance_id_;
  std::unordered_map<uint64_t, std::thread> clients_;
  // Map from channel id to MMIO region offset
  // syncs with set_memory_window to register channel id properly
  std::unordered_map<ChannelId, uint64_t> channel_offsets_;
  // Function and service registration
  uint64_t curr_function_identifier = 0;
  bool has_registered_service = false;
  using FunctionHandler =
      std::function<void(void *args_region, void *result_region)>;
  std::unordered_map<FunctionEnum, FunctionInfo> function_registry_;

private:
  // Assume each channel size is 256MB
  static constexpr uint64_t CHANNEL_SIZE = 0x10000000;
  // Dumb fast easy way to get next offset to test
  uint64_t get_next_channel_offset() {
    uint64_t next_offset = 0;
    if (channel_offsets_.empty()) {
      return next_offset;
    }
    for (const auto& [channel_id, offset] : channel_offsets_) {
      if (offset >= next_offset) {
        next_offset = offset + CHANNEL_SIZE;
      }
    }
    return next_offset;
  }

  void handle_new_client(std::unique_ptr<AbstractCXLConnection> conn) {
    uint64_t mmio_offset = get_next_channel_offset();
    channel_offsets_[conn->get_channel_id()] = mmio_offset;
    set_memory_window(mmio_offset, conn->get_size(),
                      conn->get_channel_id());
    uint64_t channel_id = conn->get_channel_id();

    std::cout << "New client connected with channel ID: "
              << conn->get_channel_id() << ", Base: " << conn->get_base()
              << ", Size: " << conn->get_size() << std::endl;

    clients_[channel_id] =
        std::thread([this, conn = std::move(conn), mmio_offset]() mutable {
          this->service_client(std::move(conn), mmio_offset);
        });
  }

  void handle_channel_close(uint64_t channel_id) {
    std::cout << "Channel with ID " << channel_id << " is being closed."
              << std::endl;

    auto it = clients_.find(channel_id);
    if (it != clients_.end()) {
      if (it->second.joinable()) {
        it->second.join();
      }
      clients_.erase(it);
      std::cout << "Channel with ID " << channel_id << " closed successfully."
                << std::endl;
    } else {
      std::cerr << "Channel with ID " << channel_id << " not found."
                << std::endl;
    }
  }

  void handle_client_disconnect(uint64_t channel_id) {
    std::cout << "Client with channel ID " << channel_id << " disconnected."
              << std::endl;

    auto it = clients_.find(channel_id);
    if (it != clients_.end()) {
      if (it->second.joinable()) {
        it->second.join();
      }
      clients_.erase(it);
    }
  }

  /// This is the function that is launched into its respective thread context
  /// TODO: Currently in failure-free domain.
  void service_client(std::unique_ptr<AbstractCXLConnection> connection, uint64_t mmio_offset) {
    // Install signal handler to ignore segfault
    signal(SIGSEGV, segfault_handler);
    signal(SIGBUS, segfault_handler);
    std::cout << "Servicing client on channel id "
              << connection->get_channel_id() << " with base address "
              << mmio_offset << " and size "
              << connection->get_size() << std::endl;
    volatile uint64_t mapped_base =
        reinterpret_cast<uint64_t>(bar2_base_) + mmio_offset;
    volatile uint64_t* q_posn = reinterpret_cast<volatile uint64_t*>(mapped_base + DiancieHeap::QUEUE_POSITION);
    QueueEntry *server_queue = reinterpret_cast<QueueEntry *>(
        mapped_base + DiancieHeap::SERVER_QUEUE_OFFSET);
    QueueEntry *client_queue = reinterpret_cast<QueueEntry *>(
        mapped_base + DiancieHeap::CLIENT_QUEUE_OFFSET);
    volatile uint64_t data_area = mapped_base + DiancieHeap::DATA_AREA_OFFSET;
    // When a server freshly picks up the service_client connection, whether brand
    // new or recover, we read the q_posn from the shm region, which is updated
    // by the server. The client always maintains a local copy.
    // q_offset is a generational counter
    volatile uint64_t q_offset = *q_posn;
    // Invariant: they only differ by 1 position at most. We assume a strict
    //            synchronous execution.
    // TODO: How to handle wrap around? Not realistic to assume only 128 RPC
    //       requests per connection.
    // We use the raw value of the gc to determine the commit flag
    bool commit_flag = !((q_offset / DiancieHeap::NUM_QUEUE_ENTRIES) % 2);
    // We modulo it to index into queue
    volatile uint64_t offset = q_offset % DiancieHeap::NUM_QUEUE_ENTRIES;
    
    while (true) {
      // TODO: Do optimized polling
      while (client_queue[offset].get_flag() != commit_flag) {
        std::this_thread::sleep_for(std::chrono::microseconds(100000));
      }
      // Get offset and abs addr from curr queue entry
      uint64_t request_offset = client_queue[offset].get_address();
      uint64_t request_addr = request_offset + data_area;

      try {

        std::cout << "Processing request at offset: " << std::hex
                  << request_offset << ", absolute address: " << request_addr
                  << std::dec << std::endl;
        // Read function identifier
        volatile FunctionEnum *func_id_ptr =
            reinterpret_cast<FunctionEnum *>(request_addr);
        FunctionEnum func_id = *func_id_ptr;
        std::cout << "Function ID: " << static_cast<uint32_t>(func_id)
                  << std::endl;

        auto it = function_registry_.find(func_id);
        if (it == function_registry_.end()) {
          throw std::invalid_argument("Invalid function identifier");
        }

        const FunctionInfo &func_info = it->second;

        size_t fid_offset = sizeof(FunctionEnum);
        size_t args_offset = fid_offset;
        size_t result_offset = args_offset + func_info.args_size;

        void *args_region =
            reinterpret_cast<void *>(request_addr + args_offset);
        void *results_region =
            reinterpret_cast<void *>(request_addr + result_offset);

        std::cout << "Server memory layout (simple):" << std::endl;
        std::cout << "  Function ID at: 0x" << std::hex << request_addr
                  << std::dec << std::endl;
        std::cout << "  args_offset: " << args_offset << std::endl;
        std::cout << "  result_offset: " << result_offset << std::endl;
        std::cout << "  Arguments at: 0x" << std::hex
                  << reinterpret_cast<uintptr_t>(args_region) << std::dec
                  << " (size: " << func_info.args_size << " bytes)"
                  << std::endl;
        std::cout << "  Results at: 0x" << std::hex
                  << reinterpret_cast<uintptr_t>(results_region) << std::dec
                  << " (size: " << func_info.result_size << " bytes)"
                  << std::endl;

        func_info.handler(args_region, results_region);
        std::cout << "Handler completed successfully " << std::endl;
        // Set results region for client to read from
        server_queue[offset].set_address(result_offset);
        // Commits request
        server_queue[offset].set_flag(commit_flag);
        // After the server has committed its response, it is safe to update
        // the server offset in the shm region.
        // We only consider the server failure for a recovery: if the client fails
        // we terminate the connection. So the client's server offset is local
        // to its closure.
        // Is it safe to update the client offset? Also yes. Cos we only consider
        // server failure for recovery. If server failed and another took over,
        // we want to look at the latest client request, which is what we wrote.
        // The client offset is local as well.
        offset = (offset + 1) % DiancieHeap::NUM_QUEUE_ENTRIES;
        *q_posn++;
        if (offset == 0) {
          commit_flag = !commit_flag;
          std::cout << "Flipping commit flag to " << commit_flag << std::endl; 
        }
        std::cout << "Processing complete. " << std::endl;
      } catch (const std::invalid_argument &e) {
        std::cerr << "Thread loop: Invalid argument " << e.what() << std::endl;
      } catch (const std::exception& e) {
        std::cerr << "Thread loop: Encountered: " << e.what() << std::endl;
        break;
      } catch (...) {
        std::cerr << "Unknown error " << std::endl;
        break;
      }

    }
    std::cout << "Thread handler closing." << std::endl;
  }

  void deregister_service() {
    std::cout << "DiancieServer: Deregistering service '" << service_name_
              << "' with instance ID '" << instance_id_ << "'" << std::endl;
    cxl_ipc_rpc_deregister_service_req_t req;
    req.type = CXL_MSG_TYPE_RPC_DEREGISTER_SERVICE_REQ;
    strncpy(req.service_name, service_name_.c_str(), MAX_SERVICE_NAME_LEN - 1);
    req.service_name[MAX_SERVICE_NAME_LEN - 1] = '\0';

    strncpy(req.instance_id, instance_id_.c_str(), MAX_INSTANCE_ID_LEN - 1);
    req.instance_id[MAX_INSTANCE_ID_LEN - 1] = '\0';

    if (send_command(&req, sizeof(req))) {
      cxl_ipc_rpc_deregister_service_resp_t resp;
      recv_response(&resp, sizeof(resp));
      if (resp.status == CXL_IPC_STATUS_OK) {
        std::cout << "DiancieServer: Service '" << service_name_
                  << "' deregistered successfully." << std::endl;
      }
    } else {
      std::cerr << "DiancieServer: Failed to send deregister command."
                << std::endl;
    }
  }
};
} // namespace diancie

#endif // DIANCIE_RPC_LIB_HPP