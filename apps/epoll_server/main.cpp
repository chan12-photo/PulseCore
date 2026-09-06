#include "pulsecore/network/epoll_echo_server.hpp"

#include <cstddef>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct ServerConfig {
  std::uint16_t port{9000};
  pulsecore::network::EpollEchoServerOptions options;
};

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

std::size_t ParsePositiveSize(std::string_view option, const char* text) {
  std::size_t parsed_chars = 0;
  const std::string input(text);
  if (input.empty() || input.front() == '-') {
    throw std::runtime_error(std::string(option) + " must be a positive integer");
  }
  try {
    const auto value = std::stoull(input, &parsed_chars);
    if (parsed_chars != input.size() || value == 0 ||
        value > std::numeric_limits<std::size_t>::max()) {
      throw std::runtime_error(std::string(option) + " must be a positive integer");
    }
    return static_cast<std::size_t>(value);
  } catch (const std::invalid_argument&) {
    throw std::runtime_error(std::string(option) + " must be a positive integer");
  } catch (const std::out_of_range&) {
    throw std::runtime_error(std::string(option) + " is out of range");
  }
}

ServerConfig ParseArgs(int argc, char** argv) {
  ServerConfig config;
  bool port_set = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view option(argv[i]);
    if (option == "--workers" || option == "--queue-capacity" ||
        option == "--max-connections" || option == "--max-in-flight" ||
        option == "--max-read-bytes" || option == "--max-write-bytes" ||
        option == "--max-input-buffer" || option == "--max-output-buffer") {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string(option) + " requires a value");
      }
      const auto value = ParsePositiveSize(option, argv[++i]);
      if (option == "--workers") {
        config.options.worker_count = value;
      } else if (option == "--queue-capacity") {
        config.options.work_queue_capacity = value;
      } else if (option == "--max-connections") {
        config.options.max_connections = value;
      } else if (option == "--max-in-flight") {
        config.options.max_in_flight_requests_per_connection = value;
      } else if (option == "--max-read-bytes") {
        config.options.max_read_bytes_per_event = value;
      } else if (option == "--max-write-bytes") {
        config.options.max_write_bytes_per_event = value;
      } else if (option == "--max-input-buffer") {
        config.options.connection_limits.max_input_buffer = value;
      } else if (option == "--max-output-buffer") {
        config.options.connection_limits.max_output_buffer = value;
      }
      continue;
    }

    if (!option.empty() && option.front() == '-') {
      throw std::runtime_error("unknown option: " + std::string(option));
    }
    if (port_set) {
      throw std::runtime_error("port specified more than once");
    }
    config.port = ParsePort(argv[i]);
    port_set = true;
  }

  return config;
}

void PrintUsage() {
  std::cerr
      << "usage: pulsecore_epoll_server [port] [--workers N] [--queue-capacity N]\n"
      << "       [--max-connections N] [--max-in-flight N] [--max-read-bytes N]\n"
      << "       [--max-write-bytes N] [--max-input-buffer N] [--max-output-buffer N]\n";
}

void PrintStartup(std::uint16_t port,
                  const pulsecore::network::EpollEchoServerOptions& options) {
  std::cout << "PulseCore epoll server listening on 127.0.0.1:" << port << '\n';
  std::cout << "options workers=" << options.worker_count
            << " queue_capacity=" << options.work_queue_capacity
            << " max_connections=" << options.max_connections
            << " max_in_flight=" << options.max_in_flight_requests_per_connection
            << " max_read_bytes=" << options.max_read_bytes_per_event
            << " max_write_bytes=" << options.max_write_bytes_per_event
            << " max_input_buffer=" << options.connection_limits.max_input_buffer
            << " max_output_buffer=" << options.connection_limits.max_output_buffer << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
      PrintUsage();
      return 0;
    }
    auto config = ParseArgs(argc, argv);
    config.options.shutdown_signals = {SIGINT, SIGTERM};

    pulsecore::network::EpollEchoServer server(config.port, config.options);
    PrintStartup(server.port(), config.options);
    server.Run();
    std::cout << "PulseCore epoll server stopped\n";
  } catch (const std::exception& error) {
    PrintUsage();
    std::cerr << "pulsecore_epoll_server: " << error.what() << '\n';
    return 1;
  }
}
