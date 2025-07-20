#ifndef TEST_RPC_INTERFACE_HPP
#define TEST_RPC_INTERFACE_HPP

#include "../includes/counter_wrapper.hpp"
#include "../includes/rpc_interface.hpp"
#include "../includes/cxl_ptr.hpp"
#include <cstdint>

using namespace diancie;

enum class TestFailFunctions : uint64_t {
  ADD,
  PERSON,
};

struct Person {
  int age;
  int income;
};

DEFINE_DIANCIE_FUNCTION(TestFailFunctions, ADD, void, global_ptr<int>);
DEFINE_DIANCIE_FUNCTION(TestFailFunctions, PERSON, void, global_ptr<Person>);

#endif