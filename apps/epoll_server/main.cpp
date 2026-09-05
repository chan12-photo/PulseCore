#include "pulsecore/network/epoll_echo_server.hpp"

#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

std::uint16_t ParsePort(const char* text) {
  const auto value = std::stoul(std::string(text));
  if (value > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("port is out of range");
  }
  return static_cast<std::uint16_t>(value);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::uint16_t port = argc >= 2 ? ParsePort(argv[1]) : 9000;

    pulsecore::network::EpollEchoServerOptions options;
    options.shutdown_signals = {SIGINT, SIGTERM};

    pulsecore::network::EpollEchoServer server(port, std::move(options));
    std::cout << "PulseCore epoll server listening on 127.0.0.1:" << server.port() << '\n';
    server.Run();
    std::cout << "PulseCore epoll server stopped\n";
  } catch (const std::exception& error) {
    std::cerr << "pulsecore_epoll_server: " << error.what() << '\n';
    return 1;
  }
}
