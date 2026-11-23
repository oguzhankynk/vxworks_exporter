#include "core/http_request.hpp"

#include <cstring>

namespace vxexp {

namespace {

constexpr char kVersionPrefix[] = "HTTP/1.";                // followed by one digit
constexpr std::size_t kVersionLen = sizeof kVersionPrefix;  // prefix + digit
constexpr char kMetricsPath[] = "/metrics";
constexpr std::size_t kMetricsPathLen = sizeof kMetricsPath - 1;

bool startsWith(const char* data, std::size_t len, const char* prefix) noexcept {
  const std::size_t n = std::strlen(prefix);
  return len >= n && std::memcmp(data, prefix, n) == 0;
}

}  // namespace

bool requestHeadComplete(const char* data, std::size_t len) noexcept {
  for (std::size_t i = 0; i + 1 < len; ++i) {
    if (data[i] == '\n' && data[i + 1] == '\n') {
      return true;
    }
    if (i + 3 < len && std::memcmp(data + i, "\r\n\r\n", 4) == 0) {
      return true;
    }
  }
  return false;
}

Route routeRequest(const char* data, std::size_t len) noexcept {
  // Limit parsing to the request line.
  std::size_t line = 0;
  while (line < len && data[line] != '\n') {
    ++line;
  }
  if (line == len) {
    return Route::BadRequest;
  }
  if (line > 0 && data[line - 1] == '\r') {
    --line;
  }

  // METHOD SP TARGET SP VERSION
  const char* method = data;
  const char* sp1 = static_cast<const char*>(std::memchr(data, ' ', line));
  if (sp1 == nullptr || sp1 == method) {
    return Route::BadRequest;
  }
  const char* target = sp1 + 1;
  const std::size_t rest = line - static_cast<std::size_t>(target - data);
  const char* sp2 = static_cast<const char*>(std::memchr(target, ' ', rest));
  if (sp2 == nullptr || sp2 == target || *target != '/') {
    return Route::BadRequest;
  }
  const char* version = sp2 + 1;
  const std::size_t version_len = line - static_cast<std::size_t>(version - data);
  if (!(version_len == kVersionLen && startsWith(version, version_len, kVersionPrefix))) {
    return Route::BadRequest;
  }

  const auto method_len = static_cast<std::size_t>(sp1 - method);
  if (!(method_len == 3 && std::memcmp(method, "GET", 3) == 0)) {
    return Route::MethodNotAllowed;
  }

  auto path_len = static_cast<std::size_t>(sp2 - target);
  const auto* query = static_cast<const char*>(std::memchr(target, '?', path_len));
  if (query != nullptr) {
    path_len = static_cast<std::size_t>(query - target);
  }
  if (path_len == 1) {
    return Route::Index;
  }
  if (path_len == kMetricsPathLen && std::memcmp(target, kMetricsPath, kMetricsPathLen) == 0) {
    return Route::Metrics;
  }
  return Route::NotFound;
}

}  // namespace vxexp
