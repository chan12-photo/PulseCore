#include "pulsecore/common/unique_fd.hpp"

#include <unistd.h>

#include <utility>

namespace pulsecore {

UniqueFd::UniqueFd(int fd) noexcept : fd_(fd) {}

UniqueFd::UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, kInvalidFd)) {}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
  if (this != &other) {
    reset(std::exchange(other.fd_, kInvalidFd));
  }
  return *this;
}

UniqueFd::~UniqueFd() noexcept {
  reset();
}

int UniqueFd::get() const noexcept {
  return fd_;
}

bool UniqueFd::is_valid() const noexcept {
  return fd_ != kInvalidFd;
}

UniqueFd::operator bool() const noexcept {
  return is_valid();
}

int UniqueFd::release() noexcept {
  return std::exchange(fd_, kInvalidFd);
}

void UniqueFd::reset(int fd) noexcept {
  if (fd_ == fd) {
    return;
  }

  if (fd_ != kInvalidFd) {
    static_cast<void>(::close(fd_));
  }
  fd_ = fd;
}

}  // namespace pulsecore
