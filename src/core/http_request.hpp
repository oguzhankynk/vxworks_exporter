#ifndef VXEXP_CORE_HTTP_REQUEST_HPP
#define VXEXP_CORE_HTTP_REQUEST_HPP

#include <cstddef>

namespace vxexp {

/// @brief What the server does with a request.
enum class Route {
  Metrics,           ///< GET /metrics: 200 with the exposition text.
  Index,             ///< GET /: 200 with a short HTML page.
  NotFound,          ///< GET of any other path: 404.
  MethodNotAllowed,  ///< Any method but GET: 405.
  BadRequest,        ///< Not an HTTP/1.x request line: 400.
};

/// @brief Largest request the server reads; longer requests get 400.
constexpr std::size_t kMaxRequestBytes = 2048;

/// @brief Whether `data` holds a complete request head (ends with an empty line).
/// @param data Bytes received so far.
/// @param len Number of bytes.
/// @return true once "\r\n\r\n" (or "\n\n") has arrived.
bool requestHeadComplete(const char* data, std::size_t len) noexcept;

/// @brief Decides the response from the request line, the first line of `data`.
///
/// Accepts "GET <path> HTTP/1.x". A query string is ignored, so
/// "/metrics?x=y" is still /metrics. Headers after the first line are not
/// looked at.
/// @param data Bytes received; need not be null-terminated.
/// @param len Number of bytes.
/// @return The route.
Route routeRequest(const char* data, std::size_t len) noexcept;

}  // namespace vxexp

#endif  // VXEXP_CORE_HTTP_REQUEST_HPP
