#include "pulsecore/network/blocking_tcp.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

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
