#ifndef DIANCIE_RPC_CLIENT_HPP
#define DIANCIE_RPC_CLIENT_HPP

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "../includes/a_cxl_connector.hpp"
#include "../includes/cxl_switch_ipc.h"
#include "../includes/ioctl_defs.h"
#include "../includes/qemu_cxl_connector.hpp"
#include "../includes/rpc_interface.hpp"
#include "../includes/mmio.hpp"
#include "../includes/cxl_ptr.hpp"

namespace diancie {

// The memory region that the client library is allocated
struct ChannelInfo {
  uint64_t offset;
  uint64_t size;
  uint64_t channel_id;
};

enum class ClientState { DISCONNECTED, CONNECTING, CONNECTED, ERROR };

static thread_local ShmContext ctx;

// The client library manages the low-level interactions for the user program.
// 1. Discover service and get allocated channel
// 2. Run background thread for heartbeat and detecting that channel has closed
template <typename FunctionEnum>
class DiancieClient : protected QEMUCXLConnector {
private:
  // Logging purposes
  std::string service_name_;
  std::string instance_id_;
  // Connection information
  uint64_t channel_id_;

  // Event loop management
  std::thread event_thread_;
  std::atomic_bool running_{false};
  std::atomic<ClientState> client_state_{ClientState::DISCONNECTED};
  // TODO: Heartbeat? But this should be QEMUCXLConnector

  mutable std::mutex state_mutex_;

  // Queue management for RPC lifecycle
  uint64_t base_addr_ = 0; // TODO: Remove
  uint64_t base_size_ = 0; // TODO: Remove
  QueueEntry *client_queue_;
  QueueEntry *server_queue_;
  uint64_t data_area_;
  // We only need one queue offset for both queues
  // Our RPC system is synchronous: there only ever
  uint64_t queue_offset_ = 0;
  uint64_t next_data_offset_ = 0;
  // How to interpret the commit flag
  bool commit_flag_ = true;

public:
  DiancieClient() = delete;
  DiancieClient(const std::string &device_path, const std::string &service_name,
                const std::string &instance_id)
      : QEMUCXLConnector(device_path), service_name_(service_name),
        instance_id_(instance_id) {
    // 1. Request channel
    auto channel_info = request_channel(service_name_, instance_id_);
    if (!channel_info.has_value()) {
      throw std::runtime_error("Failed to request channel!");
    }

    channel_id_ = channel_info->channel_id;
    // Unfortunately, I have to violate abstraction barrier here to facilitate
    // testing of testing
    // TODO: Remove soon
    base_addr_ = reinterpret_cast<uint64_t>(bar2_base_) + channel_info->offset;
    base_size_ = channel_info->size;

    client_queue_ = reinterpret_cast<QueueEntry *>(
        base_addr_ + DiancieHeap::CLIENT_QUEUE_OFFSET);
    server_queue_ = reinterpret_cast<QueueEntry *>(
        base_addr_ + DiancieHeap::SERVER_QUEUE_OFFSET);
    data_area_ = base_addr_ + DiancieHeap::DATA_AREA_OFFSET;
    next_data_offset_ = 0;

    // 2. Map mem window
    if (!set_memory_window(channel_info->offset, channel_info->size,
                           channel_info->channel_id)) {
      release_channel();
      throw std::runtime_error("Failed to set memory window for channel!");
    }

    // 3. ShmContext - sets the data area_ for the dedicated thread
    ctx.set_data_area(reinterpret_cast<void*>(data_area_));

    // Future: Heartbeat management?
    set_state(ClientState::CONNECTED);
    start_event_loop();

    std::cout << "DiancieClient initialized." << std::endl;
  }

  ~DiancieClient() {
    stop_event_loop();
    release_channel();
    std::cout << "DiancieClient resources cleaned up." << std::endl;
  }

  DiancieClient(const DiancieClient &) = delete;
  DiancieClient &operator=(const DiancieClient &) = delete;

  template <FunctionEnum func_id, typename... Args>
  auto call(Args &&...args) ->
      typename DiancieFunctionTraits<FunctionEnum, func_id>::ReturnType {
    using Traits = DiancieFunctionTraits<FunctionEnum, func_id>;
    using RetType = typename Traits::ReturnType;
    using ArgsTuple = typename Traits::ArgsTuple;

    static_assert(std::is_same_v<ArgsTuple, std::tuple<std::decay_t<Args>...>>,
                  "Argument types do not match RPC function signature.");
    if (get_state() != ClientState::CONNECTED) {
      throw std::runtime_error("Client not connected");
    }

    std::cout << "Making RPC call to function " << Traits::name << " with ID "
              << static_cast<uint32_t>(func_id) << std::endl;

    std::cout << "ctx is " << ctx.get_data_area() << std::endl;

    // ... | Function Id | Args | Result | ...
    constexpr size_t fid_size = sizeof(FunctionEnum);
    constexpr size_t args_size = sizeof(ArgsTuple);
    constexpr size_t result_size =
        std::is_void_v<RetType> ? 0 : sizeof(RetType);
    const size_t total_size = fid_size + args_size + result_size;
    // Client does not care about q_posns.
    uint64_t request_base = data_area_ + next_data_offset_;
    FunctionEnum *func_id_ptr = reinterpret_cast<FunctionEnum *>(request_base);
    void *args_region =
        reinterpret_cast<void *>(request_base + fid_size);
    void *result_region = reinterpret_cast<void *>(
        request_base + fid_size + args_size);

    std::cout << "Simple memory layout:" << std::endl;
    std::cout << "  func_id_ptr: 0x" << std::hex
              << reinterpret_cast<uintptr_t>(func_id_ptr) << std::dec
              << std::endl;
    std::cout << "  args_region: 0x" << std::hex
              << reinterpret_cast<uintptr_t>(args_region) << std::dec
              << std::endl;
    std::cout << "  result_region: 0x" << std::hex
              << reinterpret_cast<uintptr_t>(result_region) << std::dec
              << std::endl;

    // Write function ID
    *func_id_ptr = func_id;
    // Write arguments directly
    write_args(args_region, std::forward<Args>(args)...);

    std::cout << "Memory layout:" << std::endl;
    std::cout << "  Function ID at: 0x" << std::hex << request_base << std::dec
              << std::endl;
    std::cout << "  Arguments at: 0x" << std::hex
              << reinterpret_cast<uintptr_t>(args_region) << std::dec
              << " (size: " << args_size << " bytes)" << std::endl;

    if constexpr (!std::is_void_v<RetType>) {
      std::cout << "  Results at: 0x" << std::hex
                << reinterpret_cast<uintptr_t>(result_region) << std::dec
                << " (size: " << result_size << " bytes)" << std::endl;
    }
    // Write the address to server queue: Recall that this is relative offset
    client_queue_[queue_offset_].set_address(next_data_offset_);
    client_queue_[queue_offset_].set_flag(commit_flag_); // signals the server
    std::cout << "Request queued at " << queue_offset_ << std::endl;
    // Wait for server's response: This is blocking as we only adopt sync model
    while (server_queue_[queue_offset_].get_flag() != commit_flag_) {
      // TODO: Optimize polling
      std::this_thread::sleep_for(std::chrono::microseconds(100000));
    }
    std::cout << "Server processing complete" << std::endl;

    next_data_offset_ += total_size;
    // // Align to 8-byte boundary
    next_data_offset_ = (next_data_offset_ + 7) & ~7ULL;
    queue_offset_ = (queue_offset_ + 1) % DiancieHeap::NUM_QUEUE_ENTRIES;
    if (queue_offset_ == 0) commit_flag_ = !commit_flag_;

    if constexpr (!std::is_void_v<RetType>) {
      return mmio_read<RetType>(result_region);
    }
  }

  // // For testing purposes, not final
  // void client_write_u64(uint64_t offset, uint64_t value);
  // uint64_t client_read_u64(uint64_t offset);

  // Conn management
  bool is_connected() const { return get_state() == ClientState::CONNECTED; }

  ClientState get_state() const { return client_state_.load(); }

private:

  // --- Write args ---
  // overload to handle all diff cases for user transparency?
  template<typename... Args>
  void write_args(void* region, Args&&... args) {
    size_t offset = 0;
    ((mmio_write(static_cast<char*>(region)+offset, args), offset += sizeof(std::decay_t<Args>)), ...);
  }

  // Setup and cleanup
  // Methods that are called by ctor and dtor
  // Find a service via the FM and request a comms channel
  std::optional<ChannelInfo> request_channel(const std::string &service_name,
                                             const std::string &instance_id) {
    cxl_ipc_rpc_request_channel_req_t req;
    std::memset(&req, 0, sizeof(req));
    req.type = CXL_MSG_TYPE_RPC_REQUEST_CHANNEL_REQ;
    strncpy(req.service_name, service_name.c_str(), MAX_SERVICE_NAME_LEN - 1);
    strncpy(req.instance_id, instance_id.c_str(), MAX_INSTANCE_ID_LEN - 1);

    std::cout << "DiancieClient: Requesting channel for service '"
              << req.service_name << "' with instance ID '" << req.instance_id
              << "'" << std::endl;

    if (send_command(&req, sizeof(req))) {
      cxl_ipc_rpc_request_channel_resp_t resp;
      recv_response(&resp, sizeof(resp));
      if (resp.status == CXL_IPC_STATUS_OK) {
        std::cout << "DiancieClient: Channel allocated successfully. "
                  << "Offset: 0x" << std::hex << resp.channel_shm_offset
                  << ", Size: 0x" << resp.channel_shm_size
                  << ", Channel ID: " << resp.channel_id << std::dec
                  << std::endl;
        return ChannelInfo{resp.channel_shm_offset, resp.channel_shm_size,
                           resp.channel_id};
      }
    }
    return std::nullopt;
  }

  bool release_channel() {
    cxl_ipc_rpc_release_channel_req_t req;
    req.type = CXL_MSG_TYPE_RPC_RELEASE_CHANNEL_REQ;
    req.channel_id = channel_id_;

    if (send_command(&req, sizeof(req))) {
      cxl_ipc_rpc_release_channel_resp_t resp;
      recv_response(&resp, sizeof(resp));
      return resp.status == CXL_IPC_STATUS_OK;
    }
    return false;
  }

  void start_event_loop() {
    if (!running_.load()) {
      running_ = true;
      event_thread_ = std::thread(&DiancieClient::client_event_loop, this);
    }

    std::cout << "DiancieClient: Event loop started." << std::endl;
  }

  void stop_event_loop() {
    if (running_.load()) {
      running_ = false;
      if (event_thread_.joinable()) {
        event_thread_.join();
      }
    }

    std::cout << "DiancieClient: Event loop stopped." << std::endl;
  }

  void client_event_loop() {
    std::cout << "DiancieClient: Starting event loop." << std::endl;
    while (running_.load()) {
      try {
        auto event = wait_for_event(1000);
        if (event) {
          switch (event->type) {
          case CXLEvent::CHANNEL_CLOSED:
            handle_channel_close();
            break;
          default:
            std::cout << "Ignoring event " << std::endl;
            break;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      } catch (const std::exception &e) {
        if (running_.load()) {
          std::cerr << "DiancieClient: Exception in event loop: " << e.what()
                    << std::endl;
        }
      }
    }
  }

  void handle_channel_close() {
    // At the moment, this is the same as a disconnect. It's probably the same
    // in the final too?
    set_state(ClientState::DISCONNECTED);
    running_ = false;
    std::cout << "DiancieClient: Channel closed. Disconnecting." << std::endl;
  }

  void set_state(ClientState new_state) {
    ClientState old_state = client_state_.exchange(new_state);
    if (old_state != new_state) {
      std::cout << "DiancieClient: State changed from "
                << static_cast<int>(old_state) << " to "
                << static_cast<int>(new_state) << std::endl;
    }
  }
// shmalloc - friend
private:
  std::vector<std::pair<uint64_t, size_t>> allocations_;
public:
  // Use a simple linear allocation scheme - assume no freeing for now
  // Make same assumption (2) as AIFM
  template<typename T>
  global_ptr<T> shm_new_(size_t count=1) {
    size_t size = sizeof(T) * count;
    size_t alignment = alignof(T);
    
    uint64_t aligned_offset = (next_data_offset_ + alignment - 1) & ~(alignment - 1);
    
    std::cout << "Allocating at " << next_data_offset_ << std::endl; 
    
    // Check for out of memory later
    allocations_.emplace_back(aligned_offset, size);
    next_data_offset_ = aligned_offset + size;

    std::cout << "Next data offset is " << next_data_offset_ << std::endl;

    return global_ptr<T>(aligned_offset, count);
  }
};

} // namespace diancie

#endif