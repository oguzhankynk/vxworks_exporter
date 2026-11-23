// Minimal HTTP/1.0 server for /metrics, running as one kernel task.
//
// Deliberately small, because it parses network input in kernel space:
// one connection at a time, GET only, a 2 KB request limit, receive and send
// timeouts, and nothing read beyond the request line. Responses are streamed
// through a fixed buffer with "Connection: close", so no Content-Length and
// no per-scrape allocation are needed.

#include "module/http_server.hpp"

#include <vxWorks.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <semLib.h>
#include <sockLib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sysLib.h>
#include <taskLib.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "core/http_request.hpp"
#include "core/metric_writer.hpp"
#include "core/sink.hpp"
#include "module/collectors.hpp"
#include "module/funcptr.hpp"

namespace vxexp::http {

namespace {

constexpr std::size_t kSendBuffer = 16 * 1024;
constexpr int kRecvTimeoutSeconds = 2;
constexpr int kSendTimeoutSeconds = 5;
constexpr std::size_t kServerStack = 32 * 1024;

std::atomic<int> listen_fd{-1};
std::atomic<bool> stopping{false};
TASK_ID server_task = TASK_ID_NULL;
SEM_ID done = SEM_ID_NULL;
char send_buffer[kSendBuffer];

bool sendAll(void* context, const char* data, std::size_t len) {
  const int fd = *static_cast<int*>(context);
  while (len > 0) {
    const ssize_t sent = send(fd, const_cast<char*>(data), len, 0);
    if (sent <= 0)
      return false;
    data += sent;
    len -= static_cast<std::size_t>(sent);
  }
  return true;
}

void setTimeout(int fd, int option, int seconds) {
  struct timeval tv;
  tv.tv_sec = seconds;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, option, reinterpret_cast<char*>(&tv), sizeof tv);
}

void respond(int fd, Route route) {
  BufferedSink sink(send_buffer, sizeof send_buffer, sendAll, &fd);
  auto put = [&sink](const char* text) {
    sink.write(text, std::strlen(text));
  };
  switch (route) {
    case Route::Metrics: {
      put("HTTP/1.0 200 OK\r\nContent-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
          "Connection: close\r\n\r\n");
      MetricWriter writer(sink);
      collectAll(writer);
      break;
    }
    case Route::Index:
      put("HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
          "<html><head><title>VxWorks Exporter</title></head><body>"
          "<h1>VxWorks Exporter</h1><p><a href=\"/metrics\">Metrics</a></p></body></html>\n");
      break;
    case Route::NotFound:
      put("HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
          "Not found. Metrics are at /metrics.\n");
      break;
    case Route::MethodNotAllowed:
      put("HTTP/1.0 405 Method Not Allowed\r\nAllow: GET\r\nContent-Type: text/plain\r\n"
          "Connection: close\r\n\r\nOnly GET is supported.\n");
      break;
    case Route::BadRequest:
      put("HTTP/1.0 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
          "Bad request.\n");
      break;
  }
  sink.flush();
}

void serve(int fd) {
  setTimeout(fd, SO_RCVTIMEO, kRecvTimeoutSeconds);
  setTimeout(fd, SO_SNDTIMEO, kSendTimeoutSeconds);
  char request[kMaxRequestBytes];
  std::size_t used = 0;
  while (used < sizeof request && !requestHeadComplete(request, used)) {
    const ssize_t n = recv(fd, request + used, sizeof request - used, 0);
    if (n <= 0)
      break;  // closed, timed out or failed
    used += static_cast<std::size_t>(n);
  }
  const Route route =
      requestHeadComplete(request, used) ? routeRequest(request, used) : Route::BadRequest;
  respond(fd, route);
}

int serverLoop(_Vx_usr_arg_t /*unused*/) {
  while (!stopping.load()) {
    const int fd = listen_fd.load();
    if (fd < 0)
      break;
    const int client = accept(fd, nullptr, nullptr);
    if (client < 0) {
      if (stopping.load())
        break;
      taskDelay(1);  // e.g. out of descriptors: do not spin
      continue;
    }
    serve(client);
    close(client);
  }
  semGive(done);
  return 0;
}

}  // namespace

bool start(const char* bind_addr, int port, int priority) {
  if (running()) {
    std::printf("vxworks_exporter: already running\n");
    return false;
  }
  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<unsigned short>(port));
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind_addr != nullptr && bind_addr[0] != '\0' &&
      inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) {
    std::printf("vxworks_exporter: '%s' is not an IPv4 address\n", bind_addr);
    return false;
  }

  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    std::printf("vxworks_exporter: socket() failed: errno %#x\n", errno);
    return false;
  }
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&one), sizeof one);
  if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof addr) != 0 || listen(fd, 4) != 0) {
    std::printf("vxworks_exporter: cannot listen on port %d: errno %#x\n", port, errno);
    close(fd);
    return false;
  }

  if (done == SEM_ID_NULL)
    done = semBCreate(SEM_Q_FIFO, SEM_EMPTY);
  stopping.store(false);
  listen_fd.store(fd);
  server_task = taskSpawn(const_cast<char*>("tVxExporter"),
                          priority,
                          0,
                          kServerStack,
                          asFuncptr(serverLoop),
                          0,
                          0,
                          0,
                          0,
                          0,
                          0,
                          0,
                          0,
                          0,
                          0);
  if (server_task == TASK_ID_ERROR) {
    std::printf("vxworks_exporter: cannot spawn the server task\n");
    listen_fd.store(-1);
    close(fd);
    server_task = TASK_ID_NULL;
    return false;
  }
  return true;
}

void stop() {
  if (!running())
    return;
  stopping.store(true);
  const int fd = listen_fd.exchange(-1);
  // Closing the socket makes the blocked accept() return.
  shutdown(fd, SHUT_RDWR);
  close(fd);
  if (semTake(done, static_cast<_Vx_ticks_t>(5 * sysClkRateGet())) != OK) {
    // Stuck in a client: end it the hard way.
    taskDelete(server_task);
  }
  server_task = TASK_ID_NULL;
}

bool running() noexcept {
  return server_task != TASK_ID_NULL;
}

}  // namespace vxexp::http
