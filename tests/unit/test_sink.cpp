#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "core/sink.hpp"

namespace vxexp {
namespace {

struct Recorder {
  std::vector<std::string> batches;
  std::size_t fail_after = static_cast<std::size_t>(-1);

  static bool deliver(void* context, const char* data, std::size_t len) {
    auto* self = static_cast<Recorder*>(context);
    if (self->batches.size() >= self->fail_after) {
      return false;
    }
    self->batches.emplace_back(data, len);
    return true;
  }

  std::string joined() const {
    std::string all;
    for (const std::string& b : batches) {
      all += b;
    }
    return all;
  }
};

TEST(BufferedSinkTest, HoldsSmallWritesUntilFlush) {
  char buffer[8];
  Recorder rec;
  BufferedSink sink(buffer, sizeof buffer, Recorder::deliver, &rec);
  sink.write("abc", 3);
  EXPECT_TRUE(rec.batches.empty());
  EXPECT_TRUE(sink.flush());
  ASSERT_EQ(rec.batches.size(), 1U);
  EXPECT_EQ(rec.batches[0], "abc");
}

TEST(BufferedSinkTest, SplitsWritesLargerThanTheBuffer) {
  char buffer[4];
  Recorder rec;
  BufferedSink sink(buffer, sizeof buffer, Recorder::deliver, &rec);
  sink.write("0123456789", 10);
  EXPECT_TRUE(sink.flush());
  EXPECT_EQ(rec.joined(), "0123456789");
  for (const std::string& b : rec.batches) {
    EXPECT_LE(b.size(), sizeof buffer);
  }
  EXPECT_EQ(sink.total(), 10U);
}

TEST(BufferedSinkTest, ExactFillIsFlushedOnNextWrite) {
  char buffer[4];
  Recorder rec;
  BufferedSink sink(buffer, sizeof buffer, Recorder::deliver, &rec);
  sink.write("abcd", 4);
  EXPECT_TRUE(rec.batches.empty());
  sink.write("e", 1);
  ASSERT_EQ(rec.batches.size(), 1U);
  EXPECT_EQ(rec.batches[0], "abcd");
}

TEST(BufferedSinkTest, EmptyFlushDeliversNothing) {
  char buffer[4];
  Recorder rec;
  BufferedSink sink(buffer, sizeof buffer, Recorder::deliver, &rec);
  EXPECT_TRUE(sink.flush());
  EXPECT_TRUE(rec.batches.empty());
}

TEST(BufferedSinkTest, StopsAfterAFailedDelivery) {
  char buffer[2];
  Recorder rec;
  rec.fail_after = 1;
  BufferedSink sink(buffer, sizeof buffer, Recorder::deliver, &rec);
  sink.write("abcdef", 6);
  EXPECT_TRUE(sink.failed());
  EXPECT_FALSE(sink.flush());
  sink.write("more", 4);
  EXPECT_EQ(rec.joined(), "ab");
}

TEST(StringSinkTest, Appends) {
  StringSink sink;
  sink.write("ab", 2);
  sink.write("c", 1);
  EXPECT_EQ(sink.text(), "abc");
}

}  // namespace
}  // namespace vxexp
