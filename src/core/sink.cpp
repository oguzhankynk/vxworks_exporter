#include "core/sink.hpp"

#include <algorithm>
#include <cstring>

namespace vxexp {

BufferedSink::BufferedSink(char* buffer,
                           std::size_t capacity,
                           FlushFn deliver,
                           void* context) noexcept
    : buffer_(buffer), capacity_(capacity), flush_(deliver), context_(context) {}

void BufferedSink::write(const char* data, std::size_t len) {
  total_ += len;
  while (len > 0 && !failed_) {
    if (used_ == capacity_ && !flush()) {
      return;
    }
    const std::size_t chunk = std::min(len, capacity_ - used_);
    std::memcpy(buffer_ + used_, data, chunk);
    used_ += chunk;
    data += chunk;
    len -= chunk;
  }
}

bool BufferedSink::flush() {
  if (failed_) {
    return false;
  }
  if (used_ > 0) {
    failed_ = !flush_(context_, buffer_, used_);
    used_ = 0;
  }
  return !failed_;
}

}  // namespace vxexp
