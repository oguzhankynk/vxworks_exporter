#include "core/metric_writer.hpp"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vxexp {

namespace {
constexpr std::size_t kDoubleText = 40;   // longest %.15g, NaN, +Inf
constexpr std::size_t kIntegerText = 24;  // longest 64-bit integer + sign
}  // namespace

char* formatDouble(double value, char* out, std::size_t size) {
  if (std::isnan(value)) {
    (void)std::snprintf(out, size, "NaN");
  } else if (std::isinf(value)) {
    (void)std::snprintf(out, size, value > 0 ? "+Inf" : "-Inf");
  } else {
    // 15 significant digits round-trip every value the exporter produces
    // (seconds with sub-microsecond resolution up to years) without the
    // noise digits of %.17g.
    (void)std::snprintf(out, size, "%.15g", value);
  }
  return out;
}

void MetricWriter::put(const char* text) {
  sink_.write(text, std::strlen(text));
}

void MetricWriter::putEscaped(const char* text, bool escape_quotes) {
  const char* run = text;
  for (const char* p = text; *p != '\0'; ++p) {
    const char* replacement = nullptr;
    if (*p == '\\') {
      replacement = "\\\\";
    } else if (*p == '\n') {
      replacement = "\\n";
    } else if (*p == '"' && escape_quotes) {
      replacement = "\\\"";
    }
    if (replacement != nullptr) {
      sink_.write(run, static_cast<std::size_t>(p - run));
      put(replacement);
      run = p + 1;
    }
  }
  put(run);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): mirrors # HELP <name> <help>
void MetricWriter::family(const char* name, const char* help, MetricType type) {
  put("# HELP ");
  put(name);
  put(" ");
  putEscaped(help, false);
  put("\n# TYPE ");
  put(name);
  put(type == MetricType::Counter ? " counter\n" : " gauge\n");
}

void MetricWriter::head(const char* name, std::initializer_list<Label> labels) {
  put(name);
  if (labels.size() == 0) {
    put(" ");
    return;
  }
  put("{");
  bool first = true;
  for (const Label& label : labels) {
    if (!first) {
      put(",");
    }
    first = false;
    put(label.name);
    put("=\"");
    putEscaped(label.value != nullptr ? label.value : "", true);
    put("\"");
  }
  put("} ");
}

void MetricWriter::sample(const char* name, std::initializer_list<Label> labels, double value) {
  char text[kDoubleText];
  head(name, labels);
  put(formatDouble(value, text, sizeof text));
  put("\n");
}

void MetricWriter::sample(const char* name,
                          std::initializer_list<Label> labels,
                          std::uint64_t value) {
  char text[kIntegerText];
  (void)std::snprintf(text, sizeof text, "%" PRIu64, value);
  head(name, labels);
  put(text);
  put("\n");
}

void MetricWriter::sample(const char* name,
                          std::initializer_list<Label> labels,
                          std::int64_t value) {
  char text[kIntegerText];
  (void)std::snprintf(text, sizeof text, "%" PRId64, value);
  head(name, labels);
  put(text);
  put("\n");
}

void MetricWriter::single(const char* name, const char* help, MetricType type, double value) {
  family(name, help, type);
  sample(name, {}, value);
}

void MetricWriter::single(const char* name,
                          const char* help,
                          MetricType type,
                          std::uint64_t value) {
  family(name, help, type);
  sample(name, {}, value);
}

}  // namespace vxexp
