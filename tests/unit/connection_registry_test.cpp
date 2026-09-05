#include "pulsecore/network/connection_registry.hpp"

#include <fcntl.h>
#include <sys/socket.h>

#include <array>
#include <stdexcept>
#include <utility>

#include <gtest/gtest.h>

namespace pulsecore::network {
namespace {

struct SocketPair {
  UniqueFd connection_end;
  UniqueFd peer_end;
};

SocketPair MakeSocketPair() {
  std::array<int, 2> fds{-1, -1};
  EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()), 0);

  return SocketPair{
      .connection_end = UniqueFd(fds[0]),
      .peer_end = UniqueFd(fds[1]),
  };
}

bool IsOpen(int fd) {
  return ::fcntl(fd, F_GETFD) != -1;
}

}  // namespace

TEST(ConnectionRegistryTest, AddReturnsOpaqueMonotonicConnectionId) {
  auto first_pair = MakeSocketPair();
  auto second_pair = MakeSocketPair();

  ConnectionRegistry registry;

  const auto first_id = registry.Add(std::move(first_pair.connection_end));
  ASSERT_TRUE(registry.Remove(first_id));

  const auto second_id = registry.Add(std::move(second_pair.connection_end));

  EXPECT_NE(first_id.value, second_id.value);
  EXPECT_EQ(second_id.value, first_id.value + 1U);
}

TEST(ConnectionRegistryTest, FindReturnsRegisteredConnectionById) {
  auto pair = MakeSocketPair();

  ConnectionRegistry registry;
  const auto id = registry.Add(std::move(pair.connection_end));

  auto* connection = registry.Find(id);

  ASSERT_NE(connection, nullptr);
  EXPECT_EQ(connection->id(), id);
  EXPECT_TRUE(IsNonBlocking(connection->fd()));
}

TEST(ConnectionRegistryTest, RemoveDropsConnectionAndClosesFd) {
  auto pair = MakeSocketPair();
  const int connection_fd = pair.connection_end.get();

  ConnectionRegistry registry;
  const auto id = registry.Add(std::move(pair.connection_end));

  ASSERT_TRUE(IsOpen(connection_fd));
  EXPECT_TRUE(registry.Remove(id));

  EXPECT_FALSE(IsOpen(connection_fd));
  EXPECT_EQ(registry.Find(id), nullptr);
  EXPECT_TRUE(registry.empty());
}

TEST(ConnectionRegistryTest, RemovingUnknownConnectionReturnsFalse) {
  ConnectionRegistry registry;

  EXPECT_FALSE(registry.Remove(ConnectionId{123}));
}

TEST(ConnectionRegistryTest, RejectsInvalidFd) {
  ConnectionRegistry registry;

  EXPECT_THROW(static_cast<void>(registry.Add(UniqueFd{})), std::invalid_argument);
}

}  // namespace pulsecore::network
