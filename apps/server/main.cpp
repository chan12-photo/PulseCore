#include "pulsecore/network/blocking_tcp.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

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
  std::cerr << "usage: pulsecore_server [port]\n";
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

    pulsecore::network::BlockingEchoServer server(port);
    std::cout << "PulseCore blocking server listening on 127.0.0.1:" << server.port()
              << '\n';

    while (true) {
      server.ServeOne();
    }
  } catch (const std::exception& error) {
    std::cerr << "pulsecore_server: " << error.what() << '\n';
    return 1;
  }
}
