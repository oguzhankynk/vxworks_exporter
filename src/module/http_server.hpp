#ifndef VXEXP_MODULE_HTTP_SERVER_HPP
#define VXEXP_MODULE_HTTP_SERVER_HPP

namespace vxexp::http {

/// @brief Binds the listening socket and spawns the server task.
/// @param bind_addr IPv4 address, or nullptr / "" for all interfaces.
/// @param port TCP port.
/// @param priority Server task priority.
/// @return true on success; false with a console message otherwise.
bool start(const char* bind_addr, int port, int priority);

/// @brief Closes the listening socket and waits for the server task to end.
void stop();

/// @brief Whether the server task is running.
/// @return true between a successful start() and stop().
bool running() noexcept;

}  // namespace vxexp::http

#endif  // VXEXP_MODULE_HTTP_SERVER_HPP
