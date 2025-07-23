#include "./test_failure_interface.hpp"
#include "../serverlib/rpcserver.hpp"
#include "../includes/cxl_ptr.hpp"
#include "../includes/wal.hpp"
#include <chrono>
#include <cstring>
#include <thread>

using namespace diancie;

void add_impl(global_ptr<int>& a) {
  WAL_BEGIN(a);
  WAL_STORE(a, *a+1);
  std::this_thread::sleep_for(std::chrono::seconds(2));
  WAL_STORE(a, *a+1);
}

void person_impl(global_ptr<Person>& p) {
  WAL_BEGIN(p);
  WAL_STORE_FIELD(Person, age, &p->age, p->age + 1);
  std::this_thread::sleep_for(std::chrono::seconds(2));
  WAL_STORE_FIELD(Person, income, &p->income, p->income + 1);
}

int main(int argc, char *argv[]) {
  try {
    const std::string device_path = "/dev/cxl_switch_client0";
    const std::string service_name = "TestService1";
    const std::string instance_id = "ClientInstance1";

    DiancieServer<TestFailFunctions> server(device_path, service_name,
                                               instance_id);

    std::cout << "\n=== Registering RPC Functions ===" << std::endl;

    server.register_rpc_function<TestFailFunctions::ADD>(add_impl);
    server.register_rpc_function<TestFailFunctions::PERSON>(person_impl);

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