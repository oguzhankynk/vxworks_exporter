#ifndef VXEXP_CORE_METRIC_WRITER_HPP
#define VXEXP_CORE_METRIC_WRITER_HPP

#include <cstdint>
#include <initializer_list>

#include "core/sink.hpp"

namespace vxexp {

/// @brief One label of a sample; both strings must outlive the call.
struct Label {
  const char* name;   ///< Label name, e.g. "task".
  const char* value;  ///< Label value; escaped on output.
};

/// @brief Prometheus metric types used by the exporter.
enum class MetricType { Counter, Gauge };

/// @brief Writes the Prometheus text exposition format (version 0.0.4).
///
/// Call family() once per metric family, then sample() for each of its
/// series, before starting the next family. Names are compile-time constants
/// in this project and are written as given; label values and help texts are
/// escaped.
class MetricWriter {
public:
  /// @param sink Receives the text; must outlive the writer.
  explicit MetricWriter(Sink& sink) noexcept : sink_(sink) {}

  /// @brief Starts a metric family: writes its # HELP and # TYPE lines.
  /// @param name Family name, e.g. "vxworks_uptime_seconds".
  /// @param help One-line description.
  /// @param type Counter or gauge.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): mirrors # HELP <name> <help>
  void family(const char* name, const char* help, MetricType type);

  /// @brief Writes one sample with a floating-point value.
  /// @param name Sample name (the family name).
  /// @param labels Labels in output order; may be empty.
  /// @param value The value; NaN and infinities are written as such.
  void sample(const char* name, std::initializer_list<Label> labels, double value);

  /// @brief Writes one sample with an unsigned integer value, exactly.
  /// @param name Sample name (the family name).
  /// @param labels Labels in output order; may be empty.
  /// @param value The value.
  void sample(const char* name, std::initializer_list<Label> labels, std::uint64_t value);

  /// @brief Writes one sample with a signed integer value, exactly.
  /// @param name Sample name (the family name).
  /// @param labels Labels in output order; may be empty.
  /// @param value The value.
  void sample(const char* name, std::initializer_list<Label> labels, std::int64_t value);

  /// @brief Writes a family with a single unlabelled sample.
  /// @param name Family and sample name.
  /// @param help One-line description.
  /// @param type Counter or gauge.
  /// @param value The value.
  void single(const char* name, const char* help, MetricType type, double value);

  /// @copydoc single(const char*, const char*, MetricType, double)
  void single(const char* name, const char* help, MetricType type, std::uint64_t value);

private:
  void put(const char* text);
  void putEscaped(const char* text, bool escape_quotes);
  void head(const char* name, std::initializer_list<Label> labels);

  Sink& sink_;
};

/// @brief Formats a double the way the exposition format expects.
/// @param value The value.
/// @param out Destination; at least 32 bytes.
/// @param size Size of `out`.
/// @return `out`.
char* formatDouble(double value, char* out, std::size_t size);

}  // namespace vxexp

#endif  // VXEXP_CORE_METRIC_WRITER_HPP
