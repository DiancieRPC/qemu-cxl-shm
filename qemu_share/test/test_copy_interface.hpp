#ifndef TEST_RPC_INTERFACE_HPP
#define TEST_RPC_INTERFACE_HPP

#include "../includes/counter_wrapper.hpp"
#include "../includes/rpc_interface.hpp"
#include <cstdint>

using namespace diancie;

// Struct test
struct Person {
  int age;
  int salary; // avoid double for now
  int kill_count;
};

enum class TestCopyFunctions : uint64_t {
  ADD,
};

using tracked_int = diancie::CounterWrapper<int>;

DEFINE_DIANCIE_FUNCTION(TestCopyFunctions, ADD, tracked_int, tracked_int, tracked_int);

#endif