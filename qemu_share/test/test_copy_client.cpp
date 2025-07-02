#include "./test_copy_interface.hpp"
#include "../clientlib/rpcclient.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <unistd.h>

using namespace diancie;

void test_basic_arithmetic(DiancieClient<TestCopyFunctions>& client) {
    std::cout << "\n=== Testing Basic Arithmetic ===" << std::endl;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> int_dist(1, 1000);

    try {
        int num_add_iterations = 1;
        for (int i = 0; i < num_add_iterations; ++i) {
            int a = int_dist(gen);
            int b = int_dist(gen);
            tracked_int ti1 {a};
            tracked_int ti2 {b};
            int expected_result = a + b;
            
            // Call the ADD function
            std::cout << " a " << a << " b " << b << std::endl;
            tracked_int result = client.call<TestCopyFunctions::ADD>(ti1, ti2);
            std::cout << "Client: " << a << " + " << b << " = " << result.get() << std::endl;

            
            // Check if the result is as expected
            assert(result.get() == expected_result);
            // sleep(1);
            std::cout << " ti1 " << std::endl;
            ti1.printStats();
            std::cout << " ti2 " << std::endl;
            ti2.printStats();
            std::cout << " result " << std::endl;
            result.printStats();
        }

        std::cout << "✓ Arithmetic tests passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Arithmetic test failed: " << e.what() << std::endl;
        throw;
    }
}

void test_basic_shm(DiancieClient<TestCopyFunctions>& client) {
    std::cout << "\n=== Testing Shared Memory Operations ===" << std::endl;
    
    try {
        
        std::cout << "\n=== Testing shm person ===" << std::endl;

        auto shm_person = client.shm_new_<Person>();
        shm_person->age = 25;
        shm_person->salary = 100;
        shm_person->kill_count = 0;

        std::cout << "Created person on shm at " << &shm_person << std::endl;

        client.call<TestCopyFunctions::PROCESS_SHM_PERSON>(shm_person);
        
        assert(shm_person->age == 26);
        assert(shm_person->salary == 200);
        assert(shm_person->kill_count == 1);

        std::cout << "Referred person on shm at " << &shm_person << std::endl;
        Person person{.age=25, .salary=100, .kill_count=0};
        std::cout << "Created person at " << &person << std::endl;
        person = client.call<TestCopyFunctions::PROCESS_PERSON>(person);

        assert(person.age == 26);
        assert(person.salary == 200);
        assert(person.kill_count == 1);

        std::cout << "\n=== Testing shm person ===" << std::endl;

        std::cout << "✓ Shared memory operations tests passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Shared memory operations test failed: " << e.what() << std::endl;
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
        DiancieClient<TestCopyFunctions> client(
            device_path, service_name, instance_id
        );
        
        std::cout << "Client connected successfully!" << std::endl;
        
        // Run test suite
        test_basic_arithmetic(client);
        test_basic_shm(client);
        
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