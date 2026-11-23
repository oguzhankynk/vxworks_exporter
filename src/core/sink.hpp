#ifndef VXEXP_CORE_SINK_HPP
#define VXEXP_CORE_SINK_HPP

#include <cstddef>
#include <string>

namespace vxexp {

/// @brief Destination for exposition text.
class Sink {
public:
  Sink() = default;
  Sink(const Sink&) = delete;
  Sink& operator=(const Sink&) = delete;
  Sink(Sink&&) = delete;
  Sink& operator=(Sink&&) = delete;
  virtual ~Sink() = default;

  /// @brief Appends `len` bytes from `data`.
  /// @param data Bytes to append; need not be null-terminated.
  /// @param len Number of bytes.
  virtual void write(const char* data, std::size_t len) = 0;
};

/// @brief Collects everything in a std::string (tests, self-test).
class StringSink final : public Sink {
public:
  /// @copydoc Sink::write
  void write(const char* data, std::size_t len) override {
    text_.append(data, len);
  }

  /// @brief Everything written so far.
  /// @return The accumulated text.
  const std::string& text() const noexcept {
    return text_;
  }

private:
  std::string text_;
};

/// @brief Batches writes in a caller-provided buffer and hands full batches to
///        a flush function (a socket send, the console). Allocates nothing.
///
/// Once a flush fails, the sink drops further output and failed() reports it:
/// a scrape whose client went away stops costing work.
class BufferedSink final : public Sink {
public:
  /// @brief Receives one batch; returns false if it could not be delivered.
  using FlushFn = bool (*)(void* context, const char* data, std::size_t len);

  /// @param buffer Storage for one batch; must outlive the sink.
  /// @param capacity Size of `buffer` in bytes; must be > 0.
  /// @param deliver Called with each full batch and by flush().
  /// @param context Passed to `deliver` unchanged.
  BufferedSink(char* buffer, std::size_t capacity, FlushFn deliver, void* context) noexcept;

  /// @copydoc Sink::write
  void write(const char* data, std::size_t len) override;

  /// @brief Delivers the buffered bytes.
  /// @return false if this or any earlier delivery failed.
  bool flush();

  /// @brief Whether a delivery has failed.
  /// @return true after the first failed flush.
  bool failed() const noexcept {
    return failed_;
  }

  /// @brief Bytes accepted so far, delivered or still buffered.
  /// @return The running byte count.
  std::size_t total() const noexcept {
    return total_;
  }

private:
  char* buffer_;
  std::size_t capacity_;
  std::size_t used_ = 0;
  std::size_t total_ = 0;
  FlushFn flush_;
  void* context_;
  bool failed_ = false;
};

}  // namespace vxexp

#endif  // VXEXP_CORE_SINK_HPP
