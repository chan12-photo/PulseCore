#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace pulsecore::core {

template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
    if (capacity_ == 0) {
      throw std::invalid_argument("bounded queue capacity must be greater than zero");
    }
  }

  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;

  [[nodiscard]] bool TryPush(T item) {
    {
      std::lock_guard lock(mutex_);
      if (closed_ || items_.size() >= capacity_) {
        return false;
      }
      items_.push_back(std::move(item));
    }

    not_empty_.notify_one();
    return true;
  }

  [[nodiscard]] std::optional<T> Pop() {
    std::unique_lock lock(mutex_);
    not_empty_.wait(lock, [this] { return closed_ || !items_.empty(); });

    if (items_.empty()) {
      return std::nullopt;
    }

    T item = std::move(items_.front());
    items_.pop_front();
    return item;
  }

  void Close() {
    {
      std::lock_guard lock(mutex_);
      closed_ = true;
    }
    not_empty_.notify_all();
  }

  [[nodiscard]] std::size_t size() const {
    std::lock_guard lock(mutex_);
    return items_.size();
  }

  [[nodiscard]] std::size_t capacity() const noexcept {
    return capacity_;
  }

  [[nodiscard]] bool closed() const {
    std::lock_guard lock(mutex_);
    return closed_;
  }

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::deque<T> items_;
  bool closed_{false};
};

}  // namespace pulsecore::core
