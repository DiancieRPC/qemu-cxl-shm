#ifndef DIANCIE_WAL_HPP
#define DIANCIE_WAL_HPP
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include "cxl_ptr.hpp"

namespace diancie {
/**

WAL Logging
Performed at the journal offset.
Each write is to a new journal entry so can overwrite existing.
Request address
|
|
\|/
...| Function Id | Journal start | Journal end | Args | Result | ...

WAL Log size is determined via journal start and journal end
Journal start is initialized by the RPC Server that makes the call
Its a relative offset
Journal end is initialized to same value as journal start

Journal end dynamically expands as the RPC function progresses thru the usage
of WAL_STORE macros (for now).

If server fails and another takes over, condition to notice the failed takeover
is that journal_start != journal_end.

Use journal_start to locate state of journal area that corresponds to WAL Log
for the RPC Function and journal_end to locate end of it.

Walk through linearly and dynamically allocate WALEntry. Format is

| WALEntry | Raw Data | WALEntry | Raw Data | ...

where the raw data corresponds to the WALEntry directly before it, and is the
UNDO value of the destructive WAL_STORE. The undo ops are then done in LIFO
order. When the RPC function is re-executed, it is as if failure did not occur.

Current limitations: This simple WAL scheme only works for store operations to
global_ptr variables, which exist inside shared memory. For external variables,
such as a write to a database, it is out of the RPC framework control. Same thing
as Flink being unable to work without a connector that also supports exactly-once.

*/

struct WALEntry {
  uint64_t val_size; // How large the object is
  uint64_t data_offset; // The offset to the object in shared memory
};

// WAL Debug logging macro
static constexpr bool WAL_DEBUG_ENABLED = true;
#define WAL_DEBUG_LOG(fmt, ...) do { \
    if (WAL_DEBUG_ENABLED) { \
        printf("[WAL_DEBUG] " fmt, ##__VA_ARGS__); \
    } \
} while(0)

// Debug wrapper functions for easier GDB debugging
template<typename VarType, typename ValueType>
inline void debug_wal_store(VarType& var, const ValueType& value, 
                           void* j_area, size_t& j_end, size_t* j_end_ptr) {
    WAL_DEBUG_LOG("j_area=%p, j_end=%zu\n", j_area, j_end);
    WAL_DEBUG_LOG("var address=%p, var size=%zu\n", &var, sizeof(value));
    
    // Debug the global_ptr object structure
    WAL_DEBUG_LOG("calling var.raw_offset()...\n");
    auto offset = var.raw_offset();
    WAL_DEBUG_LOG("var.raw_offset()=%lu\n", offset);
    
    if (!j_area) {
        WAL_DEBUG_LOG("ERROR - journal area is null\n");
        throw std::runtime_error("WAL_STORE: journal area is null");
    }
    
    WALEntry entry{sizeof(value), offset};
    WAL_DEBUG_LOG("entry.val_size=%lu, entry.data_offset=%lu\n", 
           entry.val_size, entry.data_offset);
    
    *reinterpret_cast<WALEntry*>(reinterpret_cast<char*>(j_area) + j_end) = entry;
    j_end += sizeof(WALEntry);
    
    // Copy the actual value, not the global_ptr struct
    ValueType current_value = *var;
    WAL_DEBUG_LOG("current value before assignment: %d\n", static_cast<int>(current_value));
    memcpy(reinterpret_cast<char*>(j_area) + j_end, &current_value, sizeof(value));
    j_end += sizeof(value);

    *j_end_ptr = j_end;
    *var = value;
    
    WAL_DEBUG_LOG("new j_end=%zu, assigned value=%zu\n", j_end, value);
}

inline void debug_wal_begin(void*& j_area, size_t& j_start, size_t*& j_end_ptr, size_t& j_end, 
                           void* first_param_addr) {
    WAL_DEBUG_LOG("first_param_addr=%p\n", first_param_addr);
    
    j_area = ShmContext::get_journal_area();
    WAL_DEBUG_LOG("j_area=%p\n", j_area);
    
    if (!j_area) {
        WAL_DEBUG_LOG("ERROR - journal area is null\n");
        throw std::runtime_error("WAL_BEGIN: journal area is null");
    }
    
    j_start = *reinterpret_cast<size_t*>(reinterpret_cast<char*>(first_param_addr) - 16);
    j_end_ptr = reinterpret_cast<size_t*>(reinterpret_cast<char*>(first_param_addr) - 8);
    
    WAL_DEBUG_LOG("j_start=%zu, j_end_ptr=%p\n", j_start, j_end_ptr);
    
    if (!j_end_ptr) {
        WAL_DEBUG_LOG("ERROR - j_end_ptr is null\n");
        throw std::runtime_error("WAL_BEGIN: j_end_ptr is null");
    }
    
    j_end = *j_end_ptr;
    WAL_DEBUG_LOG("j_end=%zu\n", j_end);
}

// Need to use ShmContext to get journal area here
// Reach into the fat pointer which is the start of the first parameter
#define WAL_BEGIN(first_param)                                                 \
  void *j_area;                                                                \
  size_t j_start;                                                              \
  size_t *j_end_ptr;                                                           \
  size_t j_end;                                                                \
  debug_wal_begin(j_area, j_start, j_end_ptr, j_end, &(first_param))

// Macro for storing to global_ptr containing primitive
#define WAL_STORE(var, value) \
  debug_wal_store(var, value, j_area, j_end, j_end_ptr)

#define WAL_END

} // namespace diancie
#endif