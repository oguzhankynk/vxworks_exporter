#include "module/collectors.hpp"

#include <vxWorks.h>

#include <ipcom_sysctl.h>
#include <memLib.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <rtpLib.h>
#include <semLib.h>
#include <sys/socket.h>
#include <sysLib.h>
#include <taskLib.h>
#include <tickLib.h>
#include <version.h>
#include <vxCpuLib.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "module/cpu_accounting.hpp"
#include "vxworks_exporter/vxworks_exporter.h"

#ifndef VXEXP_MAX_TASKS
#define VXEXP_MAX_TASKS 2048
#endif

namespace vxexp {

namespace {

constexpr int kMaxTasks = VXEXP_MAX_TASKS;
constexpr int kMaxRtps = 256;

// Task status bits (private/taskLibP.h in the kernel headers).
constexpr UINT kSuspend = 0x01;
constexpr UINT kPend = 0x02;
constexpr UINT kDelay = 0x04;
constexpr UINT kDead = 0x08;
constexpr UINT kStop = 0x10;

const char* const kStates[] = {"ready", "pend", "delay", "suspend", "stop", "dead", "other"};
constexpr int kStateCount = static_cast<int>(sizeof kStates / sizeof kStates[0]);

int stateIndex(UINT status) noexcept {
  if (status == 0)
    return 0;
  if ((status & kStop) != 0)
    return 4;
  if ((status & kSuspend) != 0)
    return 3;
  if ((status & kPend) != 0)
    return 1;
  if ((status & kDelay) != 0)
    return 2;
  if ((status & kDead) != 0)
    return 5;
  return 6;
}

struct TaskInfo {
  TASK_ID id;
  char id_text[20];
  char name[VX_TASK_NAME_LENGTH + 1];
  int rtp;  // index into rtps, or -1 for kernel tasks
  int priority;
  int state;
  std::uint64_t stack_size;
  std::uint64_t stack_high;
  double cpu_seconds;
};

struct RtpInfo {
  RTP_ID id;
  char id_text[20];
  char path[VX_RTP_NAME_LENGTH + 1];
  std::int64_t tasks;
};

// One scrape at a time (guarded by `mutex`), so these can be static: no
// allocation per scrape, and no large stack frames in the server task.
TASK_ID ids[kMaxTasks];
TaskInfo tasks[kMaxTasks];
RtpInfo rtps[kMaxRtps];
int task_count;
int rtp_count;
std::uint64_t scrapes;
double last_duration;

SEM_ID mutex() {
  static SEM_ID sem = semMCreate(SEM_Q_PRIORITY | SEM_INVERSION_SAFE | SEM_DELETE_SAFE);
  return sem;
}

int rtpIndex(RTP_ID id) {
  for (int i = 0; i < rtp_count; ++i) {
    if (rtps[i].id == id)
      return i;
  }
  if (rtp_count == kMaxRtps)
    return -1;
  RTP_DESC desc;
  if (rtpInfoGet(id, &desc) != OK)
    return -1;  // kernel tasks
  RtpInfo& rtp = rtps[rtp_count];
  rtp.id = id;
  std::snprintf(rtp.id_text, sizeof rtp.id_text, "%#" PRIxPTR, reinterpret_cast<uintptr_t>(id));
  std::snprintf(rtp.path, sizeof rtp.path, "%s", desc.pathName);
  rtp.tasks = desc.taskCnt;
  return rtp_count++;
}

void snapshot() {
  task_count = 0;
  rtp_count = 0;
  const int listed = taskIdListGet(ids, kMaxTasks);
  for (int i = 0; i < listed; ++i) {
    TASK_DESC desc;
    if (taskInfoGet(ids[i], &desc) != OK)
      continue;  // exited meanwhile
    TaskInfo& t = tasks[task_count++];
    t.id = ids[i];
    std::snprintf(t.id_text, sizeof t.id_text, "%#" PRIxPTR, reinterpret_cast<uintptr_t>(ids[i]));
    std::snprintf(t.name, sizeof t.name, "%s", desc.td_name);
    t.rtp = desc.td_rtpId != nullptr ? rtpIndex(desc.td_rtpId) : -1;
    t.priority = desc.td_priority;
    t.state = stateIndex(desc.td_status);
    t.stack_size = desc.td_stackSize;
    t.stack_high = desc.td_stackHigh;
    t.cpu_seconds = cpu::running() ? cpu::taskSeconds(ids[i]) : 0.0;
  }
}

const char* rtpLabel(const TaskInfo& t) {
  return t.rtp >= 0 ? rtps[t.rtp].path : "kernel";
}

void systemInfo(MetricWriter& out) {
  out.family("vxworks_build_info",
             "VxWorks release, kernel flavour and board; always 1.",
             MetricType::Gauge);
  out.sample("vxworks_build_info",
             {{"release", releaseVersion}, {"kernel", vxWorksVersion}, {"model", sysModel()}},
             std::uint64_t{1});
  const auto rate = static_cast<std::uint64_t>(sysClkRateGet());
  out.single("vxworks_uptime_seconds",
             "Time since the system clock started.",
             MetricType::Gauge,
             static_cast<double>(tick64Get()) / static_cast<double>(rate > 0 ? rate : 1));
  out.single("vxworks_clock_rate_hz", "System clock tick rate.", MetricType::Gauge, rate);
  out.single("vxworks_cpus",
             "Configured CPUs.",
             MetricType::Gauge,
             static_cast<std::uint64_t>(vxCpuConfiguredGet()));
}

void memory(MetricWriter& out) {
  MEM_PART_STATS stats;
  if (memInfoGet(&stats) != OK)
    return;
  out.single("vxworks_kernel_heap_free_bytes",
             "Free bytes in the kernel heap.",
             MetricType::Gauge,
             std::uint64_t{stats.numBytesFree});
  out.single("vxworks_kernel_heap_allocated_bytes",
             "Allocated bytes in the kernel heap.",
             MetricType::Gauge,
             std::uint64_t{stats.numBytesAlloc});
  out.single("vxworks_kernel_heap_allocated_max_bytes",
             "Most bytes ever allocated at once in the kernel heap.",
             MetricType::Gauge,
             std::uint64_t{stats.maxBytesAlloc});
  out.single("vxworks_kernel_heap_largest_free_block_bytes",
             "Largest free block in the kernel heap (fragmentation indicator).",
             MetricType::Gauge,
             std::uint64_t{stats.maxBlockSizeFree});
  out.single("vxworks_kernel_heap_free_blocks",
             "Free blocks in the kernel heap.",
             MetricType::Gauge,
             std::uint64_t{stats.numBlocksFree});
}

void taskTotals(MetricWriter& out) {
  std::int64_t per_state[kStateCount] = {};
  for (int i = 0; i < task_count; ++i) ++per_state[tasks[i].state];
  out.family("vxworks_tasks", "Tasks by state.", MetricType::Gauge);
  for (int s = 0; s < kStateCount; ++s) {
    out.sample("vxworks_tasks", {{"state", kStates[s]}}, per_state[s]);
  }

  if (!cpu::running())
    return;
  out.family("vxworks_cpu_idle_seconds_total",
             "Time each CPU spent in its idle task.",
             MetricType::Counter);
  for (int i = 0; i < task_count; ++i) {
    const TaskInfo& t = tasks[i];
    if (std::strncmp(t.name, "tIdleTask", 9) == 0) {
      out.sample("vxworks_cpu_idle_seconds_total", {{"cpu", t.name + 9}}, t.cpu_seconds);
    }
  }
}

void perTask(MetricWriter& out) {
  if (cpu::running()) {
    out.family("vxworks_task_cpu_seconds_total",
               "CPU time used by each task since the exporter started (includes interrupts "
               "taken while it ran).",
               MetricType::Counter);
    for (int i = 0; i < task_count; ++i) {
      const TaskInfo& t = tasks[i];
      out.sample("vxworks_task_cpu_seconds_total",
                 {{"task", t.name}, {"task_id", t.id_text}, {"rtp", rtpLabel(t)}},
                 t.cpu_seconds);
    }
  }
  out.family(
      "vxworks_task_priority", "Current priority of each task (0 is highest).", MetricType::Gauge);
  for (int i = 0; i < task_count; ++i) {
    const TaskInfo& t = tasks[i];
    out.sample("vxworks_task_priority",
               {{"task", t.name}, {"task_id", t.id_text}, {"rtp", rtpLabel(t)}},
               std::int64_t{t.priority});
  }
  out.family("vxworks_task_state", "Current state of each task; always 1.", MetricType::Gauge);
  for (int i = 0; i < task_count; ++i) {
    const TaskInfo& t = tasks[i];
    out.sample("vxworks_task_state",
               {{"task", t.name}, {"task_id", t.id_text}, {"state", kStates[t.state]}},
               std::uint64_t{1});
  }
  out.family("vxworks_task_stack_size_bytes", "Stack size of each task.", MetricType::Gauge);
  for (int i = 0; i < task_count; ++i) {
    const TaskInfo& t = tasks[i];
    out.sample(
        "vxworks_task_stack_size_bytes", {{"task", t.name}, {"task_id", t.id_text}}, t.stack_size);
  }
  out.family("vxworks_task_stack_used_max_bytes",
             "Most stack each task has used (high-water mark).",
             MetricType::Gauge);
  for (int i = 0; i < task_count; ++i) {
    const TaskInfo& t = tasks[i];
    out.sample("vxworks_task_stack_used_max_bytes",
               {{"task", t.name}, {"task_id", t.id_text}},
               t.stack_high);
  }
}

void processes(MetricWriter& out) {
  out.single("vxworks_rtps",
             "Running real-time processes.",
             MetricType::Gauge,
             static_cast<std::uint64_t>(rtp_count));
  if (rtp_count == 0)
    return;
  out.family("vxworks_rtp_tasks", "Tasks in each real-time process.", MetricType::Gauge);
  for (int i = 0; i < rtp_count; ++i) {
    out.sample(
        "vxworks_rtp_tasks", {{"rtp_id", rtps[i].id_text}, {"path", rtps[i].path}}, rtps[i].tasks);
  }
}

// Interface counters come from IPNet's routing sysctl (the BSD NET_RT_IFLIST
// dump): one RTM_IFINFO message per interface carries its struct if_data,
// followed by a sockaddr_dl with its name. getifaddrs() would be simpler, but
// IPNet leaves ifa_data empty.
struct Interface {
  char name[IF_NAMESIZE];
  struct if_data data;
};
constexpr int kMaxInterfaces = 32;
Interface interfaces[kMaxInterfaces];
char iflist[32 * 1024];

int readInterfaces() {
  int mib[] = {IP_CTL_NET, AF_ROUTE, 0, 0, IP_NET_RT_IFLIST, 0};
  std::size_t len = sizeof iflist;
  if (ipcom_sysctl(mib, sizeof mib / sizeof mib[0], iflist, &len, nullptr, 0) != 0)
    return 0;
  int count = 0;
  std::size_t off = 0;
  while (off + sizeof(struct if_msghdr) <= len && count < kMaxInterfaces) {
    struct if_msghdr msg;
    std::memcpy(&msg, iflist + off, sizeof msg);
    if (msg.ifm_msglen == 0 || off + msg.ifm_msglen > len)
      break;
    if (msg.ifm_type == RTM_IFINFO) {
      Interface& itf = interfaces[count++];
      itf.data = msg.ifm_data;
      std::snprintf(itf.name, sizeof itf.name, "if%u", static_cast<unsigned>(msg.ifm_index));
      const std::size_t dl_off = off + sizeof msg;
      if ((msg.ifm_addrs & RTA_IFP) != 0 && dl_off + 8 <= off + msg.ifm_msglen) {
        struct sockaddr_dl dl;
        std::memset(&dl, 0, sizeof dl);
        std::memcpy(&dl,
                    iflist + dl_off,
                    std::min(sizeof dl, static_cast<std::size_t>(off + msg.ifm_msglen - dl_off)));
        if (dl.sdl_family == AF_LINK && dl.sdl_nlen > 0 && dl.sdl_nlen < sizeof itf.name &&
            dl_off + 8 + dl.sdl_nlen <= off + msg.ifm_msglen) {
          std::memcpy(itf.name, iflist + dl_off + 8, dl.sdl_nlen);  // sdl_data follows 8 bytes
          itf.name[dl.sdl_nlen] = '\0';
        }
      }
    }
    off += msg.ifm_msglen;
  }
  return count;
}

void network(MetricWriter& out) {
  const int count = readInterfaces();
  if (count == 0)
    return;

  struct Family {
    const char* name;
    const char* help;
  };
  static const Family families[] = {
      {"vxworks_network_receive_bytes_total", "Bytes received by each interface."},
      {"vxworks_network_transmit_bytes_total", "Bytes sent by each interface."},
      {"vxworks_network_receive_packets_total", "Packets received by each interface."},
      {"vxworks_network_transmit_packets_total", "Packets sent by each interface."},
      {"vxworks_network_receive_errors_total", "Receive errors on each interface."},
      {"vxworks_network_transmit_errors_total", "Transmit errors on each interface."},
      {"vxworks_network_receive_drop_total", "Packets dropped on input by each interface."},
  };
  for (std::size_t f = 0; f < sizeof families / sizeof families[0]; ++f) {
    out.family(families[f].name, families[f].help, MetricType::Counter);
    for (int i = 0; i < count; ++i) {
      const struct if_data& d = interfaces[i].data;
      const std::uint64_t values[] = {d.ifi_ibytes,
                                      d.ifi_obytes,
                                      d.ifi_ipackets,
                                      d.ifi_opackets,
                                      d.ifi_ierrors,
                                      d.ifi_oerrors,
                                      d.ifi_iqdrops};
      out.sample(families[f].name, {{"device", interfaces[i].name}}, values[f]);
    }
  }
  out.family("vxworks_network_up", "1 if the interface is up.", MetricType::Gauge);
  for (int i = 0; i < count; ++i) {
    out.sample("vxworks_network_up",
               {{"device", interfaces[i].name}},
               std::uint64_t{interfaces[i].data.ifi_link_state == LINK_STATE_DOWN ? 0U : 1U});
  }
}

void self(MetricWriter& out) {
  out.single("vxexporter_scrapes_total",
             "Metric collections, including this one.",
             MetricType::Counter,
             scrapes);
  out.single("vxexporter_scrape_duration_seconds",
             "Duration of the previous collection.",
             MetricType::Gauge,
             last_duration);
  out.single("vxexporter_cpu_accounting_enabled",
             "1 while per-task CPU time is being measured (the exporter is running).",
             MetricType::Gauge,
             std::uint64_t{cpu::running() ? 1U : 0U});
  out.single("vxexporter_cpu_table_overflows_total",
             "Context switches whose task could not be counted because the table was full.",
             MetricType::Counter,
             cpu::overflows());
  out.single("vxexporter_tasks_truncated",
             "1 if the task list was cut at VXEXP_MAX_TASKS in this collection.",
             MetricType::Gauge,
             std::uint64_t{task_count >= kMaxTasks ? 1U : 0U});
}

double clockSeconds() {
  const double tsc = cpu::nowSeconds();
  if (tsc > 0.0)
    return tsc;
  const auto rate = static_cast<double>(sysClkRateGet());
  return static_cast<double>(tick64Get()) / (rate > 0 ? rate : 1.0);
}

}  // namespace

void collectAll(MetricWriter& out) {
  SEM_ID sem = mutex();
  if (sem != SEM_ID_NULL)
    semTake(sem, WAIT_FOREVER);
  const double started = clockSeconds();
  ++scrapes;

  snapshot();
  systemInfo(out);
  memory(out);
  taskTotals(out);
  if (vxExporterPerTask != 0)
    perTask(out);
  processes(out);
  network(out);
  self(out);

  last_duration = clockSeconds() - started;
  if (sem != SEM_ID_NULL)
    semGive(sem);
}

}  // namespace vxexp
