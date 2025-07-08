#ifndef TEST_RPC_INTERFACE_HPP
#define TEST_RPC_INTERFACE_HPP

#include "../includes/counter_wrapper.hpp"
#include "../includes/rpc_interface.hpp"
#include "../includes/cxl_ptr.hpp"
#include <cstdint>

using namespace diancie;

enum class TestFailFunctions : uint64_t {
  ADD,
};

DEFINE_DIANCIE_FUNCTION(TestFailFunctions, ADD, void, global_ptr<int>);

#endif