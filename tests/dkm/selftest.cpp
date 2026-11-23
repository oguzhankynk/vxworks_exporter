// Kernel-module self-test, run on VxWorks by ctest (vxrun --dkm):
//
//   -> ld < vxexp_selftest.out
//   -> vxExporterSelfTest
//   value = 0 = 0x0          (0 = every check passed)
//
// Exercises the real kernel paths: collectors, CPU accounting with the switch
// hook, the HTTP server over loopback, and stopping cleanly.

#include <vxWorks.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sockLib.h>
#include <sys/socket.h>
#include <sysLib.h>
#include <taskLib.h>
#include <tickLib.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "core/exposition_check.hpp"
#include "core/metric_writer.hpp"
#include "core/sink.hpp"
#include "module/collectors.hpp"
#include "module/cpu_accounting.hpp"
#include "module/funcptr.hpp"
#include "vxworks_exporter/vxworks_exporter.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  std::printf("[%s] %s\n", ok ? "  OK  " : " FAIL ", what.c_str());
  if (!ok)
    ++failures;
}

std::string collect() {
  vxexp::StringSink sink;
  vxexp::MetricWriter writer(sink);
  vxexp::collectAll(writer);
  return sink.text();
}

double sampleValue(const vxexp::ExpositionCheck& c,
                   const char* name,
                   const char* label,
                   const char* value) {
  for (const auto& s : c.samples) {
    auto it = s.labels.find(label);
    if (s.name == name && it != s.labels.end() && it->second == value)
      return s.value;
  }
  return -1.0;
}

volatile unsigned long spin_sink;

// Busy for about `ms` milliseconds of wall-clock time, then exits.
int burn(_Vx_usr_arg_t ms) {
  const _Vx_ticks64_t end = tick64Get() + static_cast<_Vx_ticks64_t>(ms) * sysClkRateGet() / 1000U;
  while (tick64Get() < end) {
    for (int i = 0; i < 10000; ++i) spin_sink = spin_sink + static_cast<unsigned long>(i);
  }
  return 0;
}

std::string httpGet(int port, const char* request) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return "";
  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<unsigned short>(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  std::string response;
  if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof addr) == 0) {
    send(fd, const_cast<char*>(request), std::strlen(request), 0);
    char buffer[1024];
    ssize_t n;
    while ((n = recv(fd, buffer, sizeof buffer, 0)) > 0) {
      response.append(buffer, static_cast<std::size_t>(n));
    }
  }
  close(fd);
  return response;
}

std::string body(const std::string& response) {
  const std::size_t end = response.find("\r\n\r\n");
  return end == std::string::npos ? "" : response.substr(end + 4);
}

}  // namespace

extern "C" int vxExporterSelfTest() {
  failures = 0;
  constexpr int kPort = 19100;

  // 1. Collection without the exporter running: valid, but no CPU families.
  {
    const vxexp::ExpositionCheck c = vxexp::checkExposition(collect());
    check(c.ok, "idle collection is valid exposition text " + c.error);
    for (const char* family : {"vxworks_build_info",
                               "vxworks_uptime_seconds",
                               "vxworks_cpus",
                               "vxworks_tasks",
                               "vxworks_kernel_heap_free_bytes",
                               "vxworks_task_priority",
                               "vxworks_task_stack_used_max_bytes",
                               "vxworks_rtps",
                               "vxexporter_scrapes_total"}) {
      check(c.hasFamily(family), std::string("has ") + family);
    }
    check(!c.hasFamily("vxworks_task_cpu_seconds_total"), "no CPU time while stopped");
  }

  // 2. Start; a busy task must accumulate CPU time.
  check(vxExporterStart("127.0.0.1", kPort) == OK, "vxExporterStart");
  check(vxExporterStart("127.0.0.1", kPort) == ERROR, "second start is refused");
  const TASK_ID busy = taskSpawn(const_cast<char*>("tSelfBusy"),
                                 254,
                                 0,
                                 16 * 1024,
                                 vxexp::asFuncptr(burn),
                                 1500,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0);
  taskDelay(static_cast<_Vx_ticks_t>(sysClkRateGet()));  // let it run about a second
  const double busy_seconds = vxexp::cpu::taskSeconds(busy);
  check(busy_seconds > 0.3,
        "busy task CPU time > 0.3 s (got " + std::to_string(busy_seconds) + ")");
  {
    const vxexp::ExpositionCheck c = vxexp::checkExposition(collect());
    check(c.ok, "running collection is valid " + c.error);
    check(c.hasFamily("vxworks_task_cpu_seconds_total"), "has vxworks_task_cpu_seconds_total");
    check(c.hasFamily("vxworks_cpu_idle_seconds_total"), "has vxworks_cpu_idle_seconds_total");
    check(sampleValue(c, "vxworks_task_cpu_seconds_total", "task", "tSelfBusy") > 0.3,
          "tSelfBusy CPU time is exported");
    check(sampleValue(c, "vxworks_tasks", "state", "ready") >= 1.0, "at least one ready task");
  }

  // 3. Per-task series can be turned off.
  vxExporterPerTask = 0;
  {
    const vxexp::ExpositionCheck c = vxexp::checkExposition(collect());
    check(c.ok && !c.hasFamily("vxworks_task_priority") && c.hasFamily("vxworks_tasks"),
          "vxExporterPerTask = 0 drops per-task series only");
  }
  vxExporterPerTask = 1;

  // 4. HTTP over loopback.
  {
    const std::string metrics = httpGet(kPort, "GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n");
    check(metrics.compare(0, 15, "HTTP/1.0 200 OK") == 0, "GET /metrics -> 200");
    check(metrics.find("text/plain; version=0.0.4") != std::string::npos,
          "exposition content type");
    const vxexp::ExpositionCheck c = vxexp::checkExposition(body(metrics));
    check(c.ok && c.hasFamily("vxworks_task_cpu_seconds_total"), "HTTP body is valid " + c.error);
    check(httpGet(kPort, "GET / HTTP/1.0\r\n\r\n").compare(0, 15, "HTTP/1.0 200 OK") == 0,
          "GET / -> 200");
    check(httpGet(kPort, "GET /nope HTTP/1.0\r\n\r\n").compare(0, 12, "HTTP/1.0 404") == 0,
          "GET /nope -> 404");
    check(httpGet(kPort, "POST /metrics HTTP/1.0\r\n\r\n").compare(0, 12, "HTTP/1.0 405") == 0,
          "POST -> 405");
    check(httpGet(kPort, "garbage\r\n\r\n").compare(0, 12, "HTTP/1.0 400") == 0, "garbage -> 400");
  }

  // 5. Stop cleanly; the port is free again and stop is not repeatable.
  check(vxExporterStop() == OK, "vxExporterStop");
  check(!vxexp::cpu::running(), "CPU accounting stopped");
  check(httpGet(kPort, "GET /metrics HTTP/1.0\r\n\r\n").empty(), "port closed after stop");
  check(vxExporterStop() == ERROR, "second stop is refused");
  check(vxExporterStart("not-an-address", kPort) == ERROR, "bad bind address is refused");
  check(!vxexp::cpu::running(), "failed start leaves no hooks behind");
  check(vxExporterStart(nullptr, kPort) == OK && vxExporterStop() == OK, "restart works");

  taskDelay(static_cast<_Vx_ticks_t>(sysClkRateGet()));  // let tSelfBusy finish
  std::printf("%d check(s) failed\n", failures);
  return failures;
}
