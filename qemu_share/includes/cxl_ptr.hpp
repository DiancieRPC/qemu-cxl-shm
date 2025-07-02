#ifndef DIANCIE_GLOBAL_PTR_HPP
#define DIANCIE_GLOBAL_PTR_HPP

#include <cstdint>
#include <stdexcept>
namespace diancie {

template <typename FunctionEnum> class DiancieClient;
template <typename FunctionEnum> class DiancieServer;

// Context for data_area for thread local usage
class ShmContext {
private:
  inline static thread_local void *data_area_ = nullptr;

public:
  ShmContext() {data_area_ = nullptr;}
  ShmContext(void *data_area) { data_area_ = data_area; }
  static void *get_data_area() { return data_area_; }
  static void set_data_area(void *data_area) { data_area_ = data_area; }
};

template <typename T> class global_ptr {
private:
  template <typename FunctionEnum> friend class DiancieClient;

  uint64_t offset_;
  size_t count_;

  global_ptr(uint64_t offset, size_t count = 1)
      : offset_(offset), count_(count) {}

public:
  global_ptr() : offset_(0), count_(0) {}

  T *local() const {
    void *data_area = ShmContext::get_data_area();
    if (!data_area) {
      return nullptr;
    }
    return reinterpret_cast<T *>(static_cast<char *>(data_area) + offset_);
  }

  T &operator*() const {
    T *ptr = local();
    if (!ptr)
      throw std::runtime_error("Dereferencing null ptr");
    return *ptr;
  }

  T *operator->() const {
    T *ptr = local();
    if (!ptr)
      throw std::runtime_error("Dereferencing null ptr");
    return ptr;
  }

  T &operator[](size_t index) const {
    if (index >= count_)
      throw std::out_of_range("index out of range");
    T *ptr = local();
    if (!ptr)
      throw std::runtime_error("Dereferencing null ptr");
    return ptr[index];
  }

  global_ptr operator+(size_t n) const {
    if (n > count_)
      throw std::out_of_range("global_ptr arithmetic out of range");
    return global_ptr(offset_ + n * sizeof(T), count_ - n);
  }

  global_ptr operator-(size_t n) const {
    return global_ptr(offset_ - n * sizeof(T), count_ + n);
  }

  global_ptr &operator++() {
    if (count_ == 0)
      throw std::out_of_range("global_ptr increment out of range");
    offset_ += sizeof(T);
    count_--;
    return *this;
  }

  global_ptr &operator--() {
    offset_ -= sizeof(T);
    count_++;
    return *this;
  }

  bool operator==(const global_ptr& other) const {
    return offset_ == other.offset_;
  }
    
  bool operator!=(const global_ptr& other) const {
    return offset_ != other.offset_;
  }

  // Transparent conversions
  operator T&() const { return *local(); }           // For reference parameters
  operator T*() const { return local(); }            // For pointer parameters  
  operator uint64_t() const { return offset_; }      // For offset parameters

  global_ptr& operator=(const T& value) {
    *local() = value;
    return *this;
  }

  uint64_t raw_offset() const { return offset_; }
  size_t count() const { return count_; }
  bool is_null() const { return offset_ == 0; }
  bool is_local() const { return ShmContext::get_data_area() != nullptr; }
   
};

} // namespace diancie

#endif