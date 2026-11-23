#ifndef VXEXP_CORE_EXPOSITION_CHECK_HPP
#define VXEXP_CORE_EXPOSITION_CHECK_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace vxexp {

/// @brief One parsed sample line.
struct ParsedSample {
  std::string name;                           ///< Metric name.
  std::map<std::string, std::string> labels;  ///< Unescaped label values.
  double value = 0.0;                         ///< Parsed value.
};

/// @brief Result of checking exposition text.
struct ExpositionCheck {
  bool ok = false;                    ///< True if the text is valid.
  std::size_t line = 0;               ///< 1-based line of the first error.
  std::string error;                  ///< Description of the first error.
  std::vector<std::string> families;  ///< Families with a # TYPE line, in order.
  std::vector<ParsedSample> samples;  ///< Every sample, in order.

  /// @brief Whether a family was declared.
  /// @param name Family name.
  /// @return true if a # TYPE line declared it.
  bool hasFamily(const std::string& name) const;
};

/// @brief Validates Prometheus text exposition format 0.0.4 and parses it.
///
/// Checks what Prometheus rejects or what this exporter must never produce:
/// name and label syntax, label-value escapes, numeric values, at most one
/// # TYPE per family, a family's # TYPE before its samples, and each family's
/// samples kept together. Used by the tests and by the target self-test; the
/// exporter itself does not need it.
/// @param text The exposition text.
/// @return The result; `ok` is false at the first violation.
ExpositionCheck checkExposition(const std::string& text);

}  // namespace vxexp

#endif  // VXEXP_CORE_EXPOSITION_CHECK_HPP
