#include "./test_copy_interface.hpp"
#include "../includes/counter_wrapper.hpp"
#include "../serverlib/rpcserver.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>
#include <thread>

using namespace diancie;

// Business logic functions - work directly with shared memory
tracked_int add_impl(tracked_int &a) {
  std::cout << "Server: Adding " << a.get() << " (zero-copy)" << std::endl;
  a.printStats();
  std::cout << "a is at address " << &a << std::endl;
  tracked_int result {a.get() + 1};
  std::cout << "Server: Result = " << result.get() << std::endl;
  return result;
}

int main(int argc, char *argv[]) {
  try {
    const std::string device_path = "/dev/cxl_switch_client0";
    const std::string service_name = "TestService1";
    const std::string instance_id = "ClientInstance1";

    DiancieServer<TestCopyFunctions> server(device_path, service_name,
                                               instance_id);

    std::cout << "\n=== Registering RPC Functions ===" << std::endl;

    server.register_rpc_function<TestCopyFunctions::ADD>(add_impl);

    std::cout << "\n=== Registering Service ===" << std::endl;
    if (!server.register_service()) {
      std::cerr << "Failed to register service!" << std::endl;
      return 1;
    }

    std::cout << "\n=== Starting Server Loop ===" << std::endl;
    std::cout << "Server ready to accept clients..." << std::endl;

    server.run_server_loop();

  } catch (const std::exception &e) {
    std::cerr << "Server error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}