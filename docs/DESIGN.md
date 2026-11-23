# vxworks_exporter: design

Status: milestones M1 to M3 are implemented (2026-11-23). This document is the
original design, updated with the decisions taken and with what implementation
showed.

Decisions (former open questions): the repository is `vxworks_exporter`, the
default port is 9100, per-task series are on by default, and the README shows
a screenshot of the demo dashboard (synthetic load, no figures).

Findings from implementation:

- IPNet's `getifaddrs()` leaves `ifa_data` empty, so interface counters come
  from `ipcom_sysctl` with `IP_NET_RT_IFLIST` (`RTM_IFINFO` messages with
  `struct if_data` and a `sockaddr_dl` name).
- `runtimeVersion` holds the kernel flavour ("SMP 64-bit"), not the release.
  `vxworks_build_info` uses `releaseVersion` ("26.09") and `vxWorksVersion`.
- The C API is `vxExporterStart/Stop/Show`, following VxWorks naming.

## 1. Goal

A [Prometheus](https://prometheus.io) exporter for VxWorks 7. It serves
`/metrics` over HTTP from the target, so Prometheus can scrape a VxWorks
system and Grafana can chart it the way `node_exporter` does for Linux.

Today VxWorks offers `spy`, `i` and `memShow`. Each prints a one-off report
to the console. None of them is machine-readable, none keeps a history, and
none can be watched remotely.

### Non-goals (v1)

- Pushing metrics (remote write, StatsD). Pull only.
- Tracing or profiling. That is the job of the later Tracy port.
- Changing kernel configuration. The exporter uses only components that are
  in the SDK kernel and in a typical VIP. It refuses to start, with a clear
  error, if one is missing.
- Publishing VxWorks performance figures. The SDK licence (NCLA 3.1(e))
  forbids distributing "Benchmarking Data". The repo ships a tool and a demo,
  but no measured overheads, latencies or comparisons.

## 2. Architecture

```
            VxWorks kernel                                   host
 ┌──────────────────────────────────────────┐
 │ switch hook ──► CPU accounting table     │
 │ (every context switch, TSC timestamps)   │
 │                                          │
 │ tVxExp task (low priority)               │   HTTP GET    ┌────────────┐
 │   accept ─► collectors ─► text format ───┼──────────────►│ Prometheus │──► Grafana
 │             tasks, memory, RTPs, CPU,    │  /metrics     └────────────┘
 │             net, exporter self-metrics   │
 └──────────────────────────────────────────┘
```

v1 is a single DKM (`vxexporter.out`). It holds the collectors and a minimal
HTTP/1.0 server in one kernel task.

Why the kernel, and not an RTP:

- Most of the data exists only in the kernel: `taskIdListGet`, `taskInfoGet`
  for every task, `memInfoGet`, `rtpInfoGet`, the task switch hook.
- An RTP-hosted server needs a kernel helper anyway (ioctl or syscall), which
  means two components and an ABI between them.

Trade-off: the HTTP parser runs in kernel space. Mitigations: GET only, a 2 KB
request cap, a receive timeout, one connection at a time, a configurable bind
address (default: all interfaces), and no parsing beyond the request line.
An RTP-hosted server is a candidate for v2 (milestone M5).

## 3. Metrics (v1)

Names follow Prometheus conventions: base units, `_total` for counters, and a
`vxworks_` prefix for target data and `vxexporter_` for self-metrics.

| Metric | Type | Labels | Source |
|---|---|---|---|
| `vxworks_build_info` | gauge (1) | `version`, `bsp`, `cpu` | `runtimeVersion`, `sysModel()` |
| `vxworks_uptime_seconds` | gauge | | `tick64Get() / sysClkRateGet()` |
| `vxworks_cpus` | gauge | | `vxCpuConfiguredGet()` |
| `vxworks_clock_rate_hz` | gauge | | `sysClkRateGet()` |
| `vxworks_task_cpu_seconds_total` | counter | `task`, `task_id`, `rtp` | switch-hook accounting (section 4) |
| `vxworks_cpu_idle_seconds_total` | counter | `cpu` | accounting of `tIdleTaskN` |
| `vxworks_tasks` | gauge | `state` | `taskIdListGet` + `td_status` |
| `vxworks_task_priority` | gauge | `task`, `task_id`, `rtp` | `td_priority` |
| `vxworks_task_state` | gauge (1) | `task`, `task_id`, `state` | `td_status` (ready, pend, delay, suspend, stop) |
| `vxworks_task_stack_size_bytes` | gauge | `task`, `task_id` | `td_stackSize` |
| `vxworks_task_stack_used_max_bytes` | gauge | `task`, `task_id` | `td_stackHigh` (high-water mark) |
| `vxworks_kernel_heap_free_bytes` | gauge | | `memInfoGet` |
| `vxworks_kernel_heap_allocated_bytes` | gauge | | `memInfoGet` |
| `vxworks_kernel_heap_allocated_max_bytes` | gauge | | `memInfoGet` |
| `vxworks_kernel_heap_largest_free_block_bytes` | gauge | | `memInfoGet` (fragmentation) |
| `vxworks_rtp_info` | gauge (1) | `rtp_id`, `path` | `rtpInfoGet` on the RTP IDs seen in the task list |
| `vxworks_rtp_tasks` | gauge | `rtp_id`, `path` | `RTP_DESC.taskCnt` |
| `vxworks_network_{receive,transmit}_bytes_total` | counter | `device` | `ipcom_sysctl(IP_NET_RT_IFLIST)` + `if_data` |
| `vxworks_network_{receive,transmit}_{packets,errors}_total` | counter | `device` | same |
| `vxexporter_scrapes_total` | counter | | |
| `vxexporter_scrape_duration_seconds` | gauge | | duration of the last scrape |
| `vxexporter_cpu_table_overflows_total` | counter | | tasks that could not be accounted |

Labels:

- Task names are not unique in VxWorks, so per-task series carry a `task_id`
  (hex) label as well.
- Tasks come and go. Their series disappear with them, which Prometheus
  handles as stale series. Deployments with many short-lived tasks can turn
  per-task series off (`perTask = 0`) and keep the aggregate ones.

Considered and left for later:

- **Per-ISR counts and CPU time:** `ISR_DESC` has `count` and `cpuTime`, but
  there is no public API to list the ISR objects. Milestone M4.
- **RTP memory usage:** it lives in each RTP's own heap and needs either an
  in-RTP agent or private kernel APIs. Milestone M5.

## 4. CPU accounting

`spy` cannot be reused: it only prints. So the exporter keeps its own
counters.

- **Hook:** a `taskSwitchHookAdd` hook. At every context switch it adds
  `now - last_switch[cpu]` to the outgoing task's counter and sets
  `last_switch[cpu] = now`. `cpu` is `vxCpuIndexGet()`, so it works on SMP.
- **Clock:** the CPU's TSC (`__rdtsc()`), calibrated at start against
  `tick64Get()` over 200 ms.
  - The kernel's 64-bit `sysTimestamp64` is not present in the SDK kernel.
  - `vxbTimestamp()` is 32-bit and would wrap within seconds, and a task that
    runs longer than one wrap between two switches would lose time.
  - Requirements: an invariant, cross-core synchronised TSC (all current
    x86-64 hardware and QEMU). v1 is x86-64 only. An arm64 port would use the
    generic timer (`CNTVCT_EL0`) in the same place.
- **Table:** fixed size (default 1024 slots, compile-time configurable),
  open addressing, keyed by `TASK_ID`, guarded by one `spinlockIsr_t`.
  - The hook does no allocation and no calls beyond the TSC read.
  - A `taskDeleteHookAdd` hook frees a task's slot.
  - If the table is full, `vxexporter_cpu_table_overflows_total` counts it.
- **Known limitation:** interrupt time is charged to the task that was
  interrupted, as with any switch-hook accounting. Documented, not hidden.
- **Prototype check:** with one busy task on the SDK kernel, the per-task
  counters summed to the wall-clock time of the window (3.02 s of CPU in
  about 3 s), and the busy task carried almost all of it.

## 5. HTTP server

- **Task:** one kernel task, `tVxExp`, at priority 250 by default.
  - The low priority is deliberate: the exporter must not disturb the
    application.
  - A saturated system shows up in Prometheus as scrape timeouts, which is
    itself a signal.
  - The priority is configurable.
- **Protocol:** HTTP/1.0, `GET /metrics`, with `Connection: close`.
  - `GET /` returns a short HTML page with a link to `/metrics`.
  - Every other request gets 404 or 405.
- **Output:** written straight into a fixed send buffer (default 16 KB) and
  flushed as it fills. No heap allocation per scrape, and no upper limit on
  the number of tasks.
- **Format:** Prometheus text exposition format 0.0.4, with escaped label
  values. OpenMetrics and protobuf are not needed.

## 6. API and configuration

Kernel shell and C API:

```c
/* start the exporter; bindAddr NULL = all interfaces, port 0 = default 9100 */
STATUS vxexporterStart (const char * bindAddr, int port);
STATUS vxexporterStop  (void);          /* removes hooks, closes the socket */
void   vxexporterShow  (void);          /* prints /metrics to the console    */
```

```
-> ld < vxexporter.out
-> vxexporterStart 0, 9100
-> vxexporterShow
```

`vxexporterShow` doubles as the DKM test entry point and as a debugging aid
when no network is available. Defaults are set by `#define`s in one header,
and can be overridden at build time with CMake cache variables.

## 7. Implementation

- **Repository:** a separate repository built on `vxworks-cpp-template`, with
  the same presets, `vxrun` and CI.
- **Language:** C++17. The code in the switch hook stays plain and
  allocation-free. No exceptions or RTTI in the kernel module.
- **Layout:** the formatter (metric families, label escaping, buffer
  flushing) is target-independent and sits behind a small `Sink` interface,
  so the host build tests it with GoogleTest and runs it under ASan and UBSan.
  The collectors are VxWorks-only.

```
include/vxexporter/   public C API (vxexporter.h)
src/format/           exposition-format writer (host + target)
src/collect/          collectors (target only): tasks, memory, rtp, cpu, net
src/http/             minimal HTTP/1.0 server (target only)
src/cpu/              switch-hook accounting (target only)
tests/unit/           host tests: format, escaping, HTTP request parsing
tests/dkm/            target self-test: vxexporterShow output is valid
tests/e2e/            boot QEMU, start exporter, scrape from host, validate
demo/                 docker compose: Prometheus + Grafana + dashboard
```

## 8. Testing

1. **Host unit tests (GoogleTest):** the format writer, label escaping, the
   HTTP request-line parser, and buffer flushing at boundaries. They run under
   `asan` and `tsan` like the template.
2. **DKM self-test (`vx-dkm`, via vxrun):** the module loads, its hooks
   install and remove cleanly, and `vxexporterShow` output parses as valid
   exposition text.
3. **End-to-end (`ctest -L e2e`):** a host-side Python test.
   - It boots the SDK kernel with `hostfwd`, loads the module, starts it and
     spawns a busy task.
   - It scrapes twice and checks: valid format, `vxworks_up`, every expected
     family, the busy task's CPU counter grows, counters never decrease, and
     the CPU delta stays within the wall-clock window.
   - vxrun needs two small additions for this: `--hostfwd` and
     `--shell-cmd`, so a test can run commands and keep the guest alive.

## 9. Milestones

| | Scope |
|---|---|
| M1 | Repo from the template; format writer and host tests; task, memory, RTP and uptime collectors; HTTP server; `vxexporterShow`; DKM self-test |
| M2 | CPU accounting (switch and delete hooks, SMP, TSC calibration); idle time per CPU; e2e test |
| M3 | Demo: `docker compose` with Prometheus and a provisioned Grafana dashboard; a `scripts/demo.sh` that boots QEMU and starts the exporter |
| M4 | Network interface counters; per-ISR metrics if a way to list ISRs turns up |
| M5 | Optional RTP-hosted server (kernel collector and syscall/ioctl bridge); RTP memory metrics |

## 10. Open questions

1. **Repository name.** `vxexporter`, or `vxworks_exporter` to follow the
   Prometheus `<system>_exporter` convention?
2. **Default port.** Use 9100, as `node_exporter` does (same role on another
   OS), or register a separate port on the Prometheus default-port wiki?
3. **Per-task series.** On by default (more useful) or off by default (lower
   cardinality)?
4. **Demo screenshots.** A Grafana screenshot of the demo (a synthetic busy
   task) is a picture of the tool, not a VxWorks benchmark. Keep it without
   numbers in the README text? (NCLA 3.1(e), see section 1.)
