#include "../includes/test_interface.hpp"
#include "../clientlib/rpcclient.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <unistd.h>

using namespace diancie;

void test_wrap_around(DiancieClient<TestServiceFunctions>& client) {
    std::cout << "\n=== Testing Wrap around ===" << std::endl;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> int_dist(1, 1000);

    try {
        int num_add_iterations = 100000;
        for (int i = 0; i < num_add_iterations; ++i) {
            int a = int_dist(gen);
            int b = int_dist(gen);
            int expected_result = a + b;
            
            std::cout << " a " << a << " b " << b << std::endl;
            int result = client.call<TestServiceFunctions::ADD>(a, b);
            std::cout << "Client: " << a << " + " << b << " = " << result << std::endl;

            // Check if the result is as expected
            assert(result == expected_result);
        }

        std::cout << "✓ Wrap around tests passed!" << std::endl;
        // // need helper functions to move structs
        // Person p1
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Wrap around test failed: " << e.what() << std::endl;
        throw;
    }
}

int main(int argc, char* argv[]) {
    try {
        const std::string device_path = "/dev/cxl_switch_client0";
        const std::string service_name = "TestService1";
        const std::string instance_id = "ClientInstance1";
        
        std::cout << "=== Test RPC Client Starting ===" << std::endl;
        std::cout << "Device path: " << device_path << std::endl;
        
        // Create client
        DiancieClient<TestServiceFunctions> client(
            device_path, service_name, instance_id
        );
        
        std::cout << "Client connected successfully!" << std::endl;
        
        // Run test suite
        test_wrap_around(client);

        std::cout << "\n=== All Tests Passed! ===" << std::endl;
        
        // Keep client alive for a bit to test disconnection
        std::cout << "Keeping client alive for 5 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        std::cout << "Test client shutting down..." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}