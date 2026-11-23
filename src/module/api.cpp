// C API of the kernel module (include/vxworks_exporter/vxworks_exporter.h).

#include <vxWorks.h>

#include <cstdio>

#include "core/metric_writer.hpp"
#include "core/sink.hpp"
#include "module/collectors.hpp"
#include "module/cpu_accounting.hpp"
#include "module/http_server.hpp"
#include "vxworks_exporter/vxworks_exporter.h"

#ifndef VXEXP_DEFAULT_PORT
#define VXEXP_DEFAULT_PORT 9100
#endif
#ifndef VXEXP_DEFAULT_PRIORITY
#define VXEXP_DEFAULT_PRIORITY 250
#endif

extern "C" {
int vxExporterPerTask = 1;
int vxExporterPriority = VXEXP_DEFAULT_PRIORITY;
}

namespace {

bool toConsole(void* /*context*/, const char* data, std::size_t len) {
  return std::fwrite(data, 1, len, stdout) == len;
}

}  // namespace

extern "C" int vxExporterStart(const char* bindAddr, int port) {
  const int effective_port = port != 0 ? port : VXEXP_DEFAULT_PORT;
  if (vxexp::http::running()) {
    std::printf("vxworks_exporter: already running\n");
    return ERROR;
  }
  if (!vxexp::cpu::start()) {
    std::printf(
        "vxworks_exporter: cannot install the task switch hook "
        "(is INCLUDE_TASK_SWITCH_HOOKS in the kernel?)\n");
    return ERROR;
  }
  if (!vxexp::http::start(bindAddr, effective_port, vxExporterPriority)) {
    vxexp::cpu::stop();
    return ERROR;
  }
  std::printf("vxworks_exporter: serving http://%s:%d/metrics\n",
              bindAddr != nullptr && bindAddr[0] != '\0' ? bindAddr : "<all interfaces>",
              effective_port);
  return OK;
}

extern "C" int vxExporterStop(void) {
  if (!vxexp::http::running()) {
    std::printf("vxworks_exporter: not running\n");
    return ERROR;
  }
  vxexp::http::stop();
  vxexp::cpu::stop();
  return OK;
}

extern "C" void vxExporterShow(void) {
  static char buffer[4096];
  vxexp::BufferedSink sink(buffer, sizeof buffer, toConsole, nullptr);
  vxexp::MetricWriter writer(sink);
  vxexp::collectAll(writer);
  sink.flush();
  std::fflush(stdout);
}
