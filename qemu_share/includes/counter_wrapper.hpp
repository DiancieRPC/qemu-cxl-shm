#ifndef COUNTER_WRAPPER_HPP
#define COUNTER_WRAPPER_HPP

#include <iostream>
#include <utility>

namespace diancie {

template<typename T>
class CounterWrapper {
private:
    T value_;
    size_t copy_count_;
    size_t move_count_;

public:
    CounterWrapper() : value_(), copy_count_(0), move_count_(0) {}
    
    explicit CounterWrapper(const T& val) : value_(val), copy_count_(0), move_count_(0) {}
    explicit CounterWrapper(T&& val) : value_(std::move(val)), copy_count_(0), move_count_(0) {}
    
    CounterWrapper(const CounterWrapper& other,
                   const char* file = __builtin_FILE(),
                   int line = __builtin_LINE()) 
        : value_(other.value_), copy_count_(other.copy_count_ + 1), move_count_(other.move_count_) {
                    std::cout << "🔄 COPY[ at " << file << ":" << line << std::endl;
        }
    
    CounterWrapper(CounterWrapper&& other) noexcept 
        : value_(std::move(other.value_)), copy_count_(other.copy_count_), move_count_(other.move_count_ + 1) {}
    
    CounterWrapper& operator=(const CounterWrapper& other) {
        if (this != &other) {
            value_ = other.value_;
            copy_count_ = other.copy_count_ + 1;
            move_count_ = other.move_count_;
            std::cout << "Copy assignment operator called " << std::endl;
        }
        return *this;
    }
    
    CounterWrapper& operator=(CounterWrapper&& other) noexcept {
        if (this != &other) {
            value_ = std::move(other.value_);
            copy_count_ = other.copy_count_;
            move_count_ = other.move_count_ + 1;
        }
        return *this;
    }
    
    T& get() { return value_; }
    const T& get() const { return value_; }
    
    T& operator*() { return value_; }
    const T& operator*() const { return value_; }
    
    T* operator->() { return &value_; }
    const T* operator->() const { return &value_; }
    
    size_t getCopyCount() const { return copy_count_; }
    size_t getMoveCount() const { return move_count_; }
    
    void resetCounters() {
        copy_count_ = 0;
        move_count_ = 0;
    }
    
    void printStats() const {
        std::cout << "Copy operations: " << copy_count_ << std::endl;
        std::cout << "Move operations: " << move_count_ << std::endl;
    }
};

}

#endif