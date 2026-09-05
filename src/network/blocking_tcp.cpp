#include "pulsecore/network/blocking_tcp.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace pulsecore::network {
namespace {

constexpr int kListenBacklog = 16;

std::runtime_error SyscallError(const char* operation, int error) {
  return std::runtime_error(std::string(operation) + ": " + std::strerror(error));
}

void ThrowLastError(const char* operation) {
  const int saved_errno = errno;
  throw SyscallError(operation, saved_errno);
}

UniqueFd CreateIpv4Socket() {
  UniqueFd fd(::socket(AF_INET, SOCK_STREAM, 0));
  if (!fd) {
    ThrowLastError("socket");
  }

#ifdef SO_NOSIGPIPE
  int no_sigpipe = 1;
  if (::setsockopt(fd.get(), SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) != 0) {
    ThrowLastError("setsockopt(SO_NOSIGPIPE)");
  }
#endif

  return fd;
}

sockaddr_in LoopbackAddress(std::uint16_t port) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  return address;
}

std::uint16_t BoundPort(int fd) {
  sockaddr_in address{};
  socklen_t length = sizeof(address);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    ThrowLastError("getsockname");
  }
  return ntohs(address.sin_port);
}

UniqueFd CreateListener(std::uint16_t port) {
  UniqueFd listener = CreateIpv4Socket();

  int reuse_address = 1;
  if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                   sizeof(reuse_address)) != 0) {
    ThrowLastError("setsockopt(SO_REUSEADDR)");
  }

  const auto address = LoopbackAddress(port);
  if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    ThrowLastError("bind");
  }

  if (::listen(listener.get(), kListenBacklog) != 0) {
    ThrowLastError("listen");
  }

  return listener;
}

UniqueFd AcceptOne(int listener_fd) {
  while (true) {
    UniqueFd client(::accept(listener_fd, nullptr, nullptr));
    if (client) {
      return client;
    }

    if (errno == EINTR) {
      continue;
    }
    ThrowLastError("accept");
  }
}

void SendAll(int fd, std::span<const protocol::Byte> bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
#ifdef MSG_NOSIGNAL
    constexpr int send_flags = MSG_NOSIGNAL;
#else
    constexpr int send_flags = 0;
#endif

    const auto remaining = bytes.size() - offset;
    const auto sent =
        ::send(fd, bytes.data() + offset, remaining, send_flags);

    if (sent < 0) {
      if (errno == EINTR) {
        continue;
      }
      ThrowLastError("send");
    }

    if (sent == 0) {
      throw std::runtime_error("send: wrote zero bytes");
    }

    offset += static_cast<std::size_t>(sent);
  }
}

std::vector<protocol::Byte> ErrorPayload(std::string_view message) {
  return {message.begin(), message.end()};
}

protocol::Message HandleRequest(const protocol::Message& request) {
  switch (request.type) {
    case protocol::MessageType::kEchoRequest:
      return protocol::Message{
          .type = protocol::MessageType::kEchoResponse,
          .request_id = request.request_id,
          .payload = request.payload,
      };
    case protocol::MessageType::kWorkRequest:
      return protocol::Message{
          .type = protocol::MessageType::kWorkResponse,
          .request_id = request.request_id,
          .payload = request.payload,
      };
    case protocol::MessageType::kEchoResponse:
    case protocol::MessageType::kWorkResponse:
    case protocol::MessageType::kErrorResponse:
      return protocol::Message{
          .type = protocol::MessageType::kErrorResponse,
          .request_id = request.request_id,
          .payload = ErrorPayload("unexpected response message from client"),
      };
  }

  return protocol::Message{
      .type = protocol::MessageType::kErrorResponse,
      .request_id = request.request_id,
      .payload = ErrorPayload("unknown request message"),
  };
}

void ServeClient(int fd) {
  protocol::FrameDecoder decoder;

  while (true) {
    auto read = ReadMessage(fd, decoder);
    if (read.status == ReadMessageStatus::kClosed ||
        read.status == ReadMessageStatus::kProtocolError) {
      return;
    }

    auto response = HandleRequest(*read.message);
    SendMessage(fd, response);
  }
}

}  // namespace

ReadMessageResult ReadMessageResult::Message(protocol::Message message) {
  return ReadMessageResult{
      .status = ReadMessageStatus::kMessage,
      .message = std::move(message),
  };
}

ReadMessageResult ReadMessageResult::Closed() noexcept {
  return ReadMessageResult{.status = ReadMessageStatus::kClosed};
}

ReadMessageResult ReadMessageResult::ProtocolError(protocol::ProtocolError error) noexcept {
  return ReadMessageResult{
      .status = ReadMessageStatus::kProtocolError,
      .protocol_error = error,
  };
}

BlockingEchoServer::BlockingEchoServer(std::uint16_t port) : listener_(CreateListener(port)) {
  port_ = BoundPort(listener_.get());
}

std::uint16_t BlockingEchoServer::port() const noexcept {
  return port_;
}

void BlockingEchoServer::ServeOne() {
  UniqueFd client = AcceptOne(listener_.get());
  ServeClient(client.get());
}

UniqueFd ConnectTcp(std::string_view host, std::uint16_t port) {
  UniqueFd client = CreateIpv4Socket();

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);

  const std::string host_string(host);
  if (::inet_pton(AF_INET, host_string.c_str(), &address.sin_addr) != 1) {
    throw std::runtime_error("inet_pton: invalid IPv4 address");
  }

  while (::connect(client.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) !=
         0) {
    if (errno == EINTR) {
      continue;
    }
    ThrowLastError("connect");
  }

  return client;
}

void SendMessage(int fd, const protocol::Message& message) {
  auto encoded = protocol::EncodeMessage(message);
  if (!encoded.has_value()) {
    throw std::runtime_error("failed to encode protocol message");
  }
  SendAll(fd, *encoded);
}

ReadMessageResult ReadMessage(int fd, protocol::FrameDecoder& decoder) {
  std::array<protocol::Byte, 4096> bytes{};

  while (true) {
    auto result = decoder.Next();
    if (result.status == protocol::DecodeStatus::kDecoded) {
      return ReadMessageResult::Message(std::move(*result.message));
    }

    if (result.status == protocol::DecodeStatus::kError) {
      return ReadMessageResult::ProtocolError(*result.error);
    }

    const auto received = ::recv(fd, bytes.data(), bytes.size(), 0);
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }
      ThrowLastError("recv");
    }

    if (received == 0) {
      return ReadMessageResult::Closed();
    }

    decoder.Append(std::span<const protocol::Byte>(bytes.data(),
                                                   static_cast<std::size_t>(received)));
  }
}

protocol::Message SendRequestAndReadResponse(std::string_view host,
                                             std::uint16_t port,
                                             const protocol::Message& request) {
  UniqueFd client = ConnectTcp(host, port);
  SendMessage(client.get(), request);

  protocol::FrameDecoder decoder;
  auto response = ReadMessage(client.get(), decoder);
  if (response.status == ReadMessageStatus::kClosed) {
    throw std::runtime_error("connection closed before response");
  }
  if (response.status == ReadMessageStatus::kProtocolError) {
    throw std::runtime_error("protocol error while reading response");
  }
  return *response.message;
}

}  // namespace pulsecore::network
