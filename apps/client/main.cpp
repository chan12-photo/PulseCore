#include "pulsecore/network/blocking_tcp.hpp"

#include "pulsecore/core/handler.hpp"

#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class ClientMessageType {
  kEcho,
  kWork,
};

struct ClientConfig {
  std::uint16_t port{0};
  std::string payload{"hello"};
  ClientMessageType message_type{ClientMessageType::kEcho};
  std::uint32_t work_iterations{1000};
};

std::uint16_t ParsePort(const char* text) {
  std::size_t parsed_chars = 0;
  const std::string input(text);
  if (input.empty() || input.front() == '-') {
    throw std::runtime_error("port must be an integer");
  }

  try {
    const auto value = std::stoul(input, &parsed_chars);
    if (parsed_chars != input.size()) {
      throw std::runtime_error("port must be an integer");
    }
    if (value > std::numeric_limits<std::uint16_t>::max()) {
      throw std::runtime_error("port is out of range");
    }
    return static_cast<std::uint16_t>(value);
  } catch (const std::invalid_argument&) {
    throw std::runtime_error("port must be an integer");
  } catch (const std::out_of_range&) {
    throw std::runtime_error("port is out of range");
  }
}

std::uint32_t ParseWorkIterations(std::string_view option, const char* text) {
  std::size_t parsed_chars = 0;
  const std::string input(text);
  if (input.empty() || input.front() == '-') {
    throw std::runtime_error(std::string(option) + " must be a positive integer");
  }

  try {
    const auto value = std::stoul(input, &parsed_chars);
    if (parsed_chars != input.size() || value == 0) {
      throw std::runtime_error(std::string(option) + " must be a positive integer");
    }
    if (value > pulsecore::core::kMaxWorkIterations) {
      throw std::runtime_error(std::string(option) + " exceeds maximum work iterations");
    }
    return static_cast<std::uint32_t>(value);
  } catch (const std::invalid_argument&) {
    throw std::runtime_error(std::string(option) + " must be a positive integer");
  } catch (const std::out_of_range&) {
    throw std::runtime_error(std::string(option) + " exceeds maximum work iterations");
  }
}

ClientMessageType ParseMessageType(std::string_view option, const char* text) {
  const std::string_view value(text);
  if (value == "echo") {
    return ClientMessageType::kEcho;
  }
  if (value == "work") {
    return ClientMessageType::kWork;
  }
  throw std::runtime_error(std::string(option) + " must be echo or work");
}

std::vector<pulsecore::protocol::Byte> PayloadBytes(std::string_view text) {
  return {text.begin(), text.end()};
}

std::string MessageTypeName(pulsecore::protocol::MessageType type) {
  switch (type) {
    case pulsecore::protocol::MessageType::kEchoRequest:
      return "echo_request";
    case pulsecore::protocol::MessageType::kEchoResponse:
      return "echo_response";
    case pulsecore::protocol::MessageType::kWorkRequest:
      return "work_request";
    case pulsecore::protocol::MessageType::kWorkResponse:
      return "work_response";
    case pulsecore::protocol::MessageType::kErrorResponse:
      return "error_response";
  }
  return "unknown";
}

std::string HexPayload(const std::vector<pulsecore::protocol::Byte>& payload) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (const auto byte : payload) {
    out << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return out.str();
}

ClientConfig ParseArgs(int argc, char** argv) {
  if (argc < 2) {
    throw std::runtime_error("missing port");
  }

  ClientConfig config;
  config.port = ParsePort(argv[1]);
  bool payload_set = false;

  for (int i = 2; i < argc; ++i) {
    const std::string_view option(argv[i]);
    if (option == "--message-type" || option == "--work-iterations") {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string(option) + " requires a value");
      }
      const char* value = argv[++i];
      if (option == "--message-type") {
        config.message_type = ParseMessageType(option, value);
      } else {
        config.work_iterations = ParseWorkIterations(option, value);
      }
      continue;
    }

    if (!option.empty() && option.front() == '-') {
      throw std::runtime_error("unknown option: " + std::string(option));
    }
    if (payload_set) {
      throw std::runtime_error("payload specified more than once");
    }
    config.payload = argv[i];
    payload_set = true;
  }

  const auto payload_size = PayloadBytes(config.payload).size();
  if (config.message_type == ClientMessageType::kEcho &&
      payload_size > pulsecore::protocol::kMaxPayloadSize) {
    throw std::runtime_error("payload exceeds protocol max payload size");
  }
  if (config.message_type == ClientMessageType::kWork &&
      payload_size > pulsecore::protocol::kMaxPayloadSize - 4U) {
    throw std::runtime_error("payload leaves no room for work request metadata");
  }

  return config;
}

pulsecore::protocol::Message BuildRequest(const ClientConfig& config) {
  auto seed = PayloadBytes(config.payload);
  if (config.message_type == ClientMessageType::kEcho) {
    return pulsecore::protocol::Message{
        .type = pulsecore::protocol::MessageType::kEchoRequest,
        .request_id = 1,
        .payload = std::move(seed),
    };
  }

  return pulsecore::protocol::Message{
      .type = pulsecore::protocol::MessageType::kWorkRequest,
      .request_id = 1,
      .payload = pulsecore::core::EncodeWorkRequestPayload(config.work_iterations, seed),
  };
}

void PrintUsage() {
  std::cerr << "usage: pulsecore_client <port> [payload] "
               "[--message-type echo|work] [--work-iterations N]\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
      PrintUsage();
      return 0;
    }
    const auto config = ParseArgs(argc, argv);
    const auto request = BuildRequest(config);

    const auto response = pulsecore::network::SendRequestAndReadResponse(
        "127.0.0.1", config.port, request);

    std::cout << "response type=" << MessageTypeName(response.type)
              << " request_id=" << response.request_id
              << " payload_hex=" << HexPayload(response.payload) << '\n';
  } catch (const std::exception& error) {
    PrintUsage();
    std::cerr << "pulsecore_client: " << error.what() << '\n';
    return 1;
  }
}
