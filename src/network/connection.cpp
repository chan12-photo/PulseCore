#include "pulsecore/network/connection.hpp"

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace pulsecore::network {
namespace {

std::runtime_error SyscallError(const char* operation, int error) {
  return std::runtime_error(std::string(operation) + ": " + std::strerror(error));
}

void ThrowLastError(const char* operation) {
  const int saved_errno = errno;
  throw SyscallError(operation, saved_errno);
}

bool IsWouldBlock(int error) noexcept {
  return error == EAGAIN || error == EWOULDBLOCK;
}

bool IsPeerClosedError(int error) noexcept {
  return error == ECONNRESET || error == EPIPE;
}

}  // namespace

ReadAvailableResult ReadAvailableResult::Ok(std::vector<protocol::Message> messages) {
  return ReadAvailableResult{
      .status = ReadAvailableStatus::kOk,
      .messages = std::move(messages),
      .protocol_error = std::nullopt,
  };
}

ReadAvailableResult ReadAvailableResult::WouldBlock() {
  return ReadAvailableResult{
      .status = ReadAvailableStatus::kWouldBlock,
      .messages = {},
      .protocol_error = std::nullopt,
  };
}

ReadAvailableResult ReadAvailableResult::PeerClosed(std::vector<protocol::Message> messages) {
  return ReadAvailableResult{
      .status = ReadAvailableStatus::kPeerClosed,
      .messages = std::move(messages),
      .protocol_error = std::nullopt,
  };
}

ReadAvailableResult ReadAvailableResult::ProtocolError(
    protocol::ProtocolError error,
    std::vector<protocol::Message> messages) {
  return ReadAvailableResult{
      .status = ReadAvailableStatus::kProtocolError,
      .messages = std::move(messages),
      .protocol_error = error,
  };
}

void SetNonBlocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    ThrowLastError("fcntl(F_GETFL)");
  }

  if ((flags & O_NONBLOCK) != 0) {
    return;
  }

  if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    ThrowLastError("fcntl(F_SETFL)");
  }
}

bool IsNonBlocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    ThrowLastError("fcntl(F_GETFL)");
  }
  return (flags & O_NONBLOCK) != 0;
}

Connection::Connection(ConnectionId id, UniqueFd fd, ConnectionLimits limits)
    : id_(id), fd_(std::move(fd)), limits_(limits) {}

ConnectionId Connection::id() const noexcept {
  return id_;
}

int Connection::fd() const noexcept {
  return fd_.get();
}

bool Connection::has_pending_output() const noexcept {
  return pending_output_bytes() > 0;
}

std::size_t Connection::pending_output_bytes() const noexcept {
  return output_buffer_.size() - output_offset_;
}

std::size_t Connection::buffered_input_bytes() const noexcept {
  return decoder_.BufferedSize();
}

ReadAvailableResult Connection::ReadAvailable(std::size_t max_read_bytes) {
  std::array<protocol::Byte, 4096> bytes{};
  std::vector<protocol::Message> messages;
  bool made_progress = false;
  std::size_t total_read = 0;

  while (true) {
    if (auto error = DrainDecoder(messages); error.has_value()) {
      return ReadAvailableResult::ProtocolError(*error, std::move(messages));
    }

    if (total_read >= max_read_bytes) {
      return ReadAvailableResult::Ok(std::move(messages));
    }

    const auto read_budget = max_read_bytes - total_read;
    const auto bytes_to_read = std::min(bytes.size(), read_budget);
    const auto received = ::recv(fd_.get(), bytes.data(), bytes_to_read, 0);
    if (received < 0) {
      const int saved_errno = errno;
      if (saved_errno == EINTR) {
        continue;
      }
      if (IsWouldBlock(saved_errno)) {
        if (made_progress || !messages.empty()) {
          return ReadAvailableResult::Ok(std::move(messages));
        }
        return ReadAvailableResult::WouldBlock();
      }
      if (IsPeerClosedError(saved_errno)) {
        return ReadAvailableResult::PeerClosed(std::move(messages));
      }
      throw SyscallError("recv", saved_errno);
    }

    if (received == 0) {
      return ReadAvailableResult::PeerClosed(std::move(messages));
    }

    made_progress = true;
    total_read += static_cast<std::size_t>(received);
    decoder_.Append(std::span<const protocol::Byte>(bytes.data(),
                                                    static_cast<std::size_t>(received)));

    if (decoder_.BufferedSize() > limits_.max_input_buffer) {
      return ReadAvailableResult::ProtocolError(protocol::ProtocolError::kPayloadTooLarge,
                                                std::move(messages));
    }
  }
}

bool Connection::QueueOutput(const protocol::Message& message) {
  auto encoded = protocol::EncodeMessage(message);
  if (!encoded.has_value()) {
    return false;
  }

  const auto pending = pending_output_bytes();
  if (pending > limits_.max_output_buffer) {
    return false;
  }

  if (encoded->size() > limits_.max_output_buffer - pending) {
    return false;
  }

  if (pending == 0) {
    output_buffer_ = std::move(*encoded);
    output_offset_ = 0;
    return true;
  }

  output_buffer_.insert(output_buffer_.end(), encoded->begin(), encoded->end());
  return true;
}

WriteAvailableResult Connection::WriteAvailable(std::size_t max_write_bytes) {
  WriteAvailableResult result{.status = WriteAvailableStatus::kOk};

  while (output_offset_ < output_buffer_.size()) {
    if (result.bytes_written >= max_write_bytes) {
      return result;
    }

#ifdef MSG_NOSIGNAL
    constexpr int send_flags = MSG_NOSIGNAL;
#else
    constexpr int send_flags = 0;
#endif

    const auto remaining_budget = max_write_bytes - result.bytes_written;
    const auto pending = output_buffer_.size() - output_offset_;
    const auto bytes_to_write = std::min(pending, remaining_budget);
    const auto sent = ::send(fd_.get(), output_buffer_.data() + output_offset_,
                             bytes_to_write, send_flags);

    if (sent < 0) {
      const int saved_errno = errno;
      if (saved_errno == EINTR) {
        continue;
      }
      if (IsWouldBlock(saved_errno)) {
        result.status = WriteAvailableStatus::kWouldBlock;
        return result;
      }
      if (IsPeerClosedError(saved_errno)) {
        result.status = WriteAvailableStatus::kPeerClosed;
        return result;
      }
      throw SyscallError("send", saved_errno);
    }

    if (sent == 0) {
      result.status = WriteAvailableStatus::kWouldBlock;
      return result;
    }

    const auto bytes_sent = static_cast<std::size_t>(sent);
    output_offset_ += bytes_sent;
    result.bytes_written += bytes_sent;
  }

  output_buffer_.clear();
  output_offset_ = 0;
  return result;
}

std::optional<protocol::ProtocolError> Connection::DrainDecoder(
    std::vector<protocol::Message>& messages) {
  while (true) {
    auto result = decoder_.Next();
    if (result.status == protocol::DecodeStatus::kNeedMoreData) {
      return std::nullopt;
    }

    if (result.status == protocol::DecodeStatus::kError) {
      return result.error;
    }

    messages.push_back(std::move(*result.message));
  }
}

}  // namespace pulsecore::network
