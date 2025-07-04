#include "./test_copy_interface.hpp"
#include "../includes/counter_wrapper.hpp"
#include "../serverlib/rpcserver.hpp"
#include "../includes/cxl_ptr.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>
#include <thread>

using namespace diancie;

// Business logic functions - work directly with shared memory
tracked_int add_impl(tracked_int &a, tracked_int &b) {
  std::cout << "Server: Adding " << a.get() << " and " << b.get() << " (zero-copy)" << std::endl;
  a.printStats();
  b.printStats();
  std::cout << "a is at address " << &a << std::endl;
  tracked_int result {a.get() + b.get()};
  std::cout << "Server: Result = " << result.get() << std::endl;
  return result;
}

// Copy from local to remote
Person process_person(Person &person) {
  std::cout << "Person addr is at " << &person << std::endl;
  person.age+=1;
  person.salary+=100;
  person.kill_count+=1;
  return person;
}

// Data already remote
void process_shm_person(global_ptr<Person> &person) {
  std::cout << "Person addr is at " << &person << std::endl;
  person->age+=1;
  person->salary+=100;
  person->kill_count+=1;
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
    server.register_rpc_function<TestCopyFunctions::PROCESS_PERSON>(process_person);
    server.register_rpc_function<TestCopyFunctions::PROCESS_SHM_PERSON>(process_shm_person);

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