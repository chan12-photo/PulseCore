#include "pulsecore/common/unique_fd.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <array>

#include <gtest/gtest.h>

namespace {

bool IsOpen(int fd) {
  return ::fcntl(fd, F_GETFD) != -1;
}

std::array<int, 2> MakePipe() {
  std::array<int, 2> fds{-1, -1};
  EXPECT_EQ(::pipe(fds.data()), 0);
  return fds;
}

}  // namespace

namespace pulsecore {

TEST(UniqueFdTest, DefaultConstructedFdIsInvalid) {
  const UniqueFd fd;

  EXPECT_FALSE(fd);
  EXPECT_FALSE(fd.is_valid());
  EXPECT_EQ(fd.get(), -1);
}

TEST(UniqueFdTest, DestructorClosesOwnedFd) {
  const auto pipe_fds = MakePipe();
  const int read_fd = pipe_fds[0];
  const int write_fd = pipe_fds[1];

  {
    const UniqueFd owned_read_fd(read_fd);
    EXPECT_TRUE(IsOpen(read_fd));
  }

  EXPECT_FALSE(IsOpen(read_fd));
  EXPECT_TRUE(IsOpen(write_fd));
  EXPECT_EQ(::close(write_fd), 0);
}

TEST(UniqueFdTest, MoveConstructorTransfersOwnership) {
  const auto pipe_fds = MakePipe();
  const int read_fd = pipe_fds[0];
  const int write_fd = pipe_fds[1];

  UniqueFd source(read_fd);
  UniqueFd target(std::move(source));

  EXPECT_FALSE(source);
  EXPECT_EQ(source.get(), -1);
  EXPECT_TRUE(target);
  EXPECT_EQ(target.get(), read_fd);

  EXPECT_EQ(::close(write_fd), 0);
}

TEST(UniqueFdTest, MoveAssignmentClosesPreviousFdAndTransfersOwnership) {
  const auto first_pipe = MakePipe();
  const auto second_pipe = MakePipe();

  const int first_read_fd = first_pipe[0];
  const int first_write_fd = first_pipe[1];
  const int second_read_fd = second_pipe[0];
  const int second_write_fd = second_pipe[1];

  UniqueFd target(first_read_fd);
  UniqueFd source(second_read_fd);

  target = std::move(source);

  EXPECT_FALSE(IsOpen(first_read_fd));
  EXPECT_FALSE(source);
  EXPECT_EQ(target.get(), second_read_fd);
  EXPECT_TRUE(IsOpen(second_read_fd));

  EXPECT_EQ(::close(first_write_fd), 0);
  EXPECT_EQ(::close(second_write_fd), 0);
}

TEST(UniqueFdTest, ReleaseReturnsFdWithoutClosingIt) {
  const auto pipe_fds = MakePipe();
  const int read_fd = pipe_fds[0];
  const int write_fd = pipe_fds[1];

  UniqueFd fd(read_fd);
  const int released = fd.release();

  EXPECT_EQ(released, read_fd);
  EXPECT_FALSE(fd);
  EXPECT_TRUE(IsOpen(read_fd));

  EXPECT_EQ(::close(read_fd), 0);
  EXPECT_EQ(::close(write_fd), 0);
}

TEST(UniqueFdTest, ResetClosesOldFdAndOwnsNewFd) {
  const auto first_pipe = MakePipe();
  const auto second_pipe = MakePipe();

  const int first_read_fd = first_pipe[0];
  const int first_write_fd = first_pipe[1];
  const int second_read_fd = second_pipe[0];
  const int second_write_fd = second_pipe[1];

  UniqueFd fd(first_read_fd);
  fd.reset(second_read_fd);

  EXPECT_FALSE(IsOpen(first_read_fd));
  EXPECT_EQ(fd.get(), second_read_fd);
  EXPECT_TRUE(IsOpen(second_read_fd));

  EXPECT_EQ(::close(first_write_fd), 0);
  EXPECT_EQ(::close(second_write_fd), 0);
}

TEST(UniqueFdTest, ResetToSameFdIsNoOp) {
  const auto pipe_fds = MakePipe();
  const int read_fd = pipe_fds[0];
  const int write_fd = pipe_fds[1];

  UniqueFd fd(read_fd);
  fd.reset(read_fd);

  EXPECT_EQ(fd.get(), read_fd);
  EXPECT_TRUE(IsOpen(read_fd));

  EXPECT_EQ(::close(write_fd), 0);
}

}  // namespace pulsecore
