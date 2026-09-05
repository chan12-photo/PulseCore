#include "pulsecore/network/epoll_echo_server.hpp"

#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::uint16_t ParsePort(const char* text) {
  std::size_t parsed_chars = 0;
  const std::string input(text);
  const auto value = std::stoul(input, &parsed_chars);
  if (parsed_chars != input.size()) {
    throw std::runtime_error("port must be an integer");
  }
  if (value > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("port is out of range");
  }
  return static_cast<std::uint16_t>(value);
}

void PrintUsage() {
  std::cerr << "usage: pulsecore_epoll_server [port]\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
      PrintUsage();
      return 0;
    }
    if (argc > 2) {
      PrintUsage();
      return 2;
    }

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
