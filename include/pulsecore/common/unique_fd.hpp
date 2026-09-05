#pragma once

namespace pulsecore {

class UniqueFd {
 public:
  UniqueFd() noexcept = default;
  explicit UniqueFd(int fd) noexcept;

  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;

  UniqueFd(UniqueFd&& other) noexcept;
  UniqueFd& operator=(UniqueFd&& other) noexcept;

  ~UniqueFd() noexcept;

  [[nodiscard]] int get() const noexcept;
  [[nodiscard]] bool is_valid() const noexcept;
  explicit operator bool() const noexcept;

  [[nodiscard]] int release() noexcept;
  void reset(int fd = kInvalidFd) noexcept;

 private:
  static constexpr int kInvalidFd = -1;

  int fd_{kInvalidFd};
};

}  // namespace pulsecore
