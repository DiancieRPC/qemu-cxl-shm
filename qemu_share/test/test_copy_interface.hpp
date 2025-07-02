#ifndef TEST_RPC_INTERFACE_HPP
#define TEST_RPC_INTERFACE_HPP

#include "../includes/counter_wrapper.hpp"
#include "../includes/rpc_interface.hpp"
#include "../includes/cxl_ptr.hpp"
#include <cstdint>

using namespace diancie;

// Struct test
struct Person {
  int age;
  int salary; // avoid double for now
  int kill_count;

  friend std::ostream& operator<<(std::ostream& os, const Person& p) {
    os << "Person{age: " << p.age << ", salary: " << p.salary << ", kill_count: " << p.kill_count << "}";
    return os;
  }
};

enum class TestCopyFunctions : uint64_t {
  ADD,
  PROCESS_PERSON,
};

using tracked_int = diancie::CounterWrapper<int>;
using tracked_person = diancie::CounterWrapper<Person>;

DEFINE_DIANCIE_FUNCTION(TestCopyFunctions, ADD, tracked_int, tracked_int, tracked_int);
DEFINE_DIANCIE_FUNCTION(TestCopyFunctions, PROCESS_PERSON, void, global_ptr<tracked_person>);

#endif