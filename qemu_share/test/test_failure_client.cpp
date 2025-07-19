#include "./test_failure_interface.hpp"
#include "../clientlib/rpcclient.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <unistd.h>

using namespace diancie;

void test_failure(DiancieClient<TestFailFunctions>& client) {
  auto a = client.shm_new_<int>();
  *a = 2;
  client.call<TestFailFunctions::ADD>(a);
  std::cout << *a << std::endl;
  assert(*a == 4);
}

int main(int argc, char* argv[]) {
    try {
        const std::string device_path = "/dev/cxl_switch_client0";
        const std::string service_name = "TestService1";
        const std::string instance_id = "ClientInstance1";
        
        std::cout << "=== Test RPC Client Starting ===" << std::endl;
        std::cout << "Device path: " << device_path << std::endl;
        
        // Create client
        DiancieClient<TestFailFunctions> client(
            device_path, service_name, instance_id
        );
        
        std::cout << "Client connected successfully!" << std::endl;
        
        // Run test suite
        test_failure(client);
        
        std::cout << "\n=== All Tests Passed! ===" << std::endl;
        
        // Keep client alive for a bit to test disconnection
        std::cout << "Keeping client alive for 5 seconds..." << std::endl;
        // std::this_thread::sleep_for(std::chrono::seconds(5));
        
        std::cout << "Test client shutting down..." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}