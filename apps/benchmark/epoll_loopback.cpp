#include "pulsecore/core/handler.hpp"
#include "pulsecore/network/blocking_tcp.hpp"
#include "pulsecore/network/epoll_echo_server.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kWorkIterationBytes = 4;

enum class BenchmarkMessageType {
  kEcho,
  kWork,
};

struct BenchmarkConfig {
  std::size_t clients{4};
  std::size_t requests_per_client{1000};
  std::size_t payload_size{64};
  std::size_t workers{std::max(1U, std::thread::hardware_concurrency() / 2U)};
  BenchmarkMessageType message_type{BenchmarkMessageType::kEcho};
  std::uint32_t work_iterations{1000};
};

struct ClientRequestTemplate {
  pulsecore::protocol::MessageType request_type;
  pulsecore::protocol::MessageType expected_response_type;
  std::vector<pulsecore::protocol::Byte> request_payload;
  std::vector<pulsecore::protocol::Byte> expected_response_payload;
};

class ServerThread {
 public:
  explicit ServerThread(pulsecore::network::EpollEchoServer& server)
      : server_(server),
        thread_([this] {
          try {
            server_.Run();
          } catch (...) {
            error_ = std::current_exception();
          }
        }) {}

  ServerThread(const ServerThread&) = delete;
  ServerThread& operator=(const ServerThread&) = delete;

  ~ServerThread() noexcept {
    server_.Stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void StopAndJoin() {
    server_.Stop();
    if (thread_.joinable()) {
      thread_.join();
    }
    if (error_ != nullptr) {
      std::rethrow_exception(error_);
    }
  }

 private:
  pulsecore::network::EpollEchoServer& server_;
  std::thread thread_;
  std::exception_ptr error_;
};

std::size_t ParsePositiveSize(std::string_view option, const char* text) {
  std::size_t parsed_chars = 0;
  const auto value = std::stoull(std::string(text), &parsed_chars);
  if (parsed_chars != std::string_view(text).size() || value == 0 ||
      value > std::numeric_limits<std::size_t>::max()) {
    throw std::runtime_error(std::string(option) + " must be a positive integer");
  }
  return static_cast<std::size_t>(value);
}

BenchmarkMessageType ParseMessageType(std::string_view option, const char* text) {
  const std::string_view value(text);
  if (value == "echo") {
    return BenchmarkMessageType::kEcho;
  }
  if (value == "work") {
    return BenchmarkMessageType::kWork;
  }
  throw std::runtime_error(std::string(option) + " must be echo or work");
}

std::uint32_t ParseWorkIterations(std::string_view option, const char* text) {
  const auto value = ParsePositiveSize(option, text);
  if (value > pulsecore::core::kMaxWorkIterations) {
    throw std::runtime_error(std::string(option) + " exceeds maximum work iterations");
  }
  return static_cast<std::uint32_t>(value);
}

BenchmarkConfig ParseArgs(int argc, char** argv) {
  BenchmarkConfig config;

  for (int i = 1; i < argc; ++i) {
    const std::string_view option(argv[i]);
    if (i + 1 >= argc) {
      throw std::runtime_error(std::string(option) + " requires a value");
    }

    const char* value = argv[++i];
    if (option == "--clients") {
      config.clients = ParsePositiveSize(option, value);
    } else if (option == "--requests-per-client") {
      config.requests_per_client = ParsePositiveSize(option, value);
    } else if (option == "--payload-size") {
      config.payload_size = ParsePositiveSize(option, value);
    } else if (option == "--workers") {
      config.workers = ParsePositiveSize(option, value);
    } else if (option == "--message-type") {
      config.message_type = ParseMessageType(option, value);
    } else if (option == "--work-iterations") {
      config.work_iterations = ParseWorkIterations(option, value);
    } else {
      throw std::runtime_error("unknown option: " + std::string(option));
    }
  }

  if (config.message_type == BenchmarkMessageType::kEcho &&
      config.payload_size > pulsecore::protocol::kMaxPayloadSize) {
    throw std::runtime_error("--payload-size exceeds protocol max payload size");
  }
  if (config.message_type == BenchmarkMessageType::kWork &&
      config.payload_size > pulsecore::protocol::kMaxPayloadSize - kWorkIterationBytes) {
    throw std::runtime_error("--payload-size leaves no room for work request metadata");
  }
  if (config.requests_per_client > std::numeric_limits<std::size_t>::max() / config.clients) {
    throw std::runtime_error("--clients * --requests-per-client is too large");
  }

  return config;
}

std::string_view MessageTypeName(BenchmarkMessageType message_type) {
  switch (message_type) {
    case BenchmarkMessageType::kEcho:
      return "echo";
    case BenchmarkMessageType::kWork:
      return "work";
  }
  return "unknown";
}

ClientRequestTemplate BuildClientRequestTemplate(std::size_t client_index,
                                                 const BenchmarkConfig& config) {
  std::vector<pulsecore::protocol::Byte> seed(config.payload_size,
                                              static_cast<pulsecore::protocol::Byte>(
                                                  client_index & 0xFFU));

  if (config.message_type == BenchmarkMessageType::kEcho) {
    return ClientRequestTemplate{
        .request_type = pulsecore::protocol::MessageType::kEchoRequest,
        .expected_response_type = pulsecore::protocol::MessageType::kEchoResponse,
        .request_payload = seed,
        .expected_response_payload = std::move(seed),
    };
  }

  auto request_payload =
      pulsecore::core::EncodeWorkRequestPayload(config.work_iterations, seed);
  const auto expected_response = pulsecore::core::HandleRequest(pulsecore::protocol::Message{
      .type = pulsecore::protocol::MessageType::kWorkRequest,
      .request_id = 0,
      .payload = request_payload,
  });
  if (expected_response.type != pulsecore::protocol::MessageType::kWorkResponse) {
    throw std::logic_error("failed to prepare expected work response");
  }

  return ClientRequestTemplate{
      .request_type = pulsecore::protocol::MessageType::kWorkRequest,
      .expected_response_type = pulsecore::protocol::MessageType::kWorkResponse,
      .request_payload = std::move(request_payload),
      .expected_response_payload = expected_response.payload,
  };
}

pulsecore::protocol::Message RequestMessage(std::uint64_t request_id,
                                            const ClientRequestTemplate& request_template) {
  return pulsecore::protocol::Message{
      .type = request_template.request_type,
      .request_id = request_id,
      .payload = request_template.request_payload,
  };
}

void RecordFirstException(std::mutex& mutex, std::exception_ptr& first_error) {
  std::lock_guard lock(mutex);
  if (first_error == nullptr) {
    first_error = std::current_exception();
  }
}

void RunClient(std::uint16_t port,
               std::size_t client_index,
               const BenchmarkConfig& config,
               std::vector<double>& latencies_seconds,
               std::atomic_size_t& ready_clients,
               std::atomic_bool& start) {
  ready_clients.fetch_add(1, std::memory_order_release);
  while (!start.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  auto socket = pulsecore::network::ConnectTcp("127.0.0.1", port);
  pulsecore::protocol::FrameDecoder decoder;
  const auto request_template = BuildClientRequestTemplate(client_index, config);

  for (std::size_t request_index = 0; request_index < config.requests_per_client;
       ++request_index) {
    const auto request_id =
        static_cast<std::uint64_t>(client_index * config.requests_per_client + request_index);
    const auto started = std::chrono::steady_clock::now();
    pulsecore::network::SendMessage(socket.get(), RequestMessage(request_id, request_template));

    auto read = pulsecore::network::ReadMessage(socket.get(), decoder);
    if (read.status != pulsecore::network::ReadMessageStatus::kMessage ||
        !read.message.has_value()) {
      throw std::runtime_error("benchmark client did not receive a response");
    }
    if (read.message->type != request_template.expected_response_type ||
        read.message->request_id != request_id ||
        read.message->payload != request_template.expected_response_payload) {
      throw std::runtime_error("benchmark client received an unexpected response");
    }
    const auto finished = std::chrono::steady_clock::now();
    latencies_seconds[request_id] = std::chrono::duration<double>(finished - started).count();
  }
}

double PercentileMicros(const std::vector<double>& sorted_seconds, std::size_t percentile) {
  if (sorted_seconds.empty()) {
    return 0.0;
  }

  const auto rank = ((percentile * sorted_seconds.size()) + 99U) / 100U;
  const auto index = std::min(sorted_seconds.size() - 1U, rank - 1U);
  return sorted_seconds[index] * 1'000'000.0;
}

void PrintUsage() {
  std::cerr << "usage: pulsecore_epoll_benchmark "
               "[--clients N] [--requests-per-client N] [--payload-size N] [--workers N] "
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

    pulsecore::network::EpollEchoServerOptions server_options;
    server_options.worker_count = config.workers;
    server_options.work_queue_capacity = std::max<std::size_t>(1024, config.clients * 8);

    pulsecore::network::EpollEchoServer server(0, std::move(server_options));
    ServerThread server_thread(server);

    std::vector<std::thread> clients;
    clients.reserve(config.clients);
    std::atomic_size_t ready_clients{0};
    std::atomic_bool start{false};
    std::mutex error_mutex;
    std::exception_ptr first_error;
    std::vector<double> latencies_seconds(config.clients * config.requests_per_client, 0.0);

    for (std::size_t client_index = 0; client_index < config.clients; ++client_index) {
      clients.emplace_back([&, client_index] {
        try {
          RunClient(server.port(), client_index, config, latencies_seconds, ready_clients, start);
        } catch (...) {
          RecordFirstException(error_mutex, first_error);
        }
      });
    }

    while (ready_clients.load(std::memory_order_acquire) < config.clients) {
      std::this_thread::yield();
    }

    const auto started = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    for (auto& client : clients) {
      client.join();
    }

    const auto finished = std::chrono::steady_clock::now();
    server_thread.StopAndJoin();

    if (first_error != nullptr) {
      std::rethrow_exception(first_error);
    }

    const auto elapsed = std::chrono::duration<double>(finished - started).count();
    const auto total_requests = config.clients * config.requests_per_client;
    const auto frame_size_template = BuildClientRequestTemplate(0, config);
    const auto round_trip_frame_bytes =
        static_cast<double>(total_requests) *
        static_cast<double>(pulsecore::protocol::kHeaderSize +
                            frame_size_template.request_payload.size() +
                            pulsecore::protocol::kHeaderSize +
                            frame_size_template.expected_response_payload.size());
    std::sort(latencies_seconds.begin(), latencies_seconds.end());

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "clients=" << config.clients << '\n';
    std::cout << "workers=" << config.workers << '\n';
    std::cout << "message_type=" << MessageTypeName(config.message_type) << '\n';
    if (config.message_type == BenchmarkMessageType::kWork) {
      std::cout << "work_iterations=" << config.work_iterations << '\n';
    }
    std::cout << "requests_per_client=" << config.requests_per_client << '\n';
    std::cout << "payload_bytes=" << config.payload_size << '\n';
    std::cout << "total_requests=" << total_requests << '\n';
    std::cout << "elapsed_seconds=" << elapsed << '\n';
    std::cout << "requests_per_second=" << static_cast<double>(total_requests) / elapsed << '\n';
    std::cout << "round_trip_frame_mib_per_second="
              << (round_trip_frame_bytes / (1024.0 * 1024.0)) / elapsed << '\n';
    std::cout << "latency_p50_us=" << PercentileMicros(latencies_seconds, 50) << '\n';
    std::cout << "latency_p95_us=" << PercentileMicros(latencies_seconds, 95) << '\n';
    std::cout << "latency_p99_us=" << PercentileMicros(latencies_seconds, 99) << '\n';
    std::cout << "latency_max_us=" << (latencies_seconds.back() * 1'000'000.0) << '\n';
  } catch (const std::exception& error) {
    PrintUsage();
    std::cerr << "pulsecore_epoll_benchmark: " << error.what() << '\n';
    return 1;
  }
}
