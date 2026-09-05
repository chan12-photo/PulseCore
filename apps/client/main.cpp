#include "pulsecore/network/blocking_tcp.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

std::vector<pulsecore::protocol::Byte> PayloadBytes(std::string_view text) {
  return {text.begin(), text.end()};
}

void PrintUsage() {
  std::cerr << "usage: pulsecore_client <port> [payload]\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
      PrintUsage();
      return 0;
    }
    if (argc < 2 || argc > 3) {
      PrintUsage();
      return 2;
    }

    const std::uint16_t port = ParsePort(argv[1]);
    const std::string payload = argc == 3 ? argv[2] : "hello";

    const auto response = pulsecore::network::SendRequestAndReadResponse(
        "127.0.0.1", port,
        pulsecore::protocol::Message{
            .type = pulsecore::protocol::MessageType::kEchoRequest,
            .request_id = 1,
            .payload = PayloadBytes(payload),
        });

    std::cout << "response type=" << static_cast<std::uint16_t>(response.type)
              << " request_id=" << response.request_id << " payload=\""
              << std::string(response.payload.begin(), response.payload.end()) << "\"\n";
  } catch (const std::exception& error) {
    std::cerr << "pulsecore_client: " << error.what() << '\n';
    return 1;
  }
}
