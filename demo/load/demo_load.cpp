// Synthetic workload for the demo dashboard: a few kernel tasks whose CPU use
// and memory use change over time, so the graphs have something to show.
// It measures nothing and is not a benchmark.
//
//   -> ld < vxexp_demo_load.out
//   -> vxExporterDemoLoad

#include <vxWorks.h>

#include <sysLib.h>
#include <taskLib.h>
#include <tickLib.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "module/funcptr.hpp"

namespace {

volatile std::uint64_t sink;

void spinTicks(_Vx_ticks64_t ticks) {
  const _Vx_ticks64_t end = tick64Get() + ticks;
  while (tick64Get() < end) {
    for (int i = 0; i < 1000; ++i) sink = sink + static_cast<std::uint64_t>(i);
  }
}

_Vx_ticks64_t ms(unsigned value) {
  return static_cast<_Vx_ticks64_t>(value) * sysClkRateGet() / 1000U + 1U;
}

// Works in bursts: 30% of each second.
int sensorTask(_Vx_usr_arg_t /*unused*/) {
  for (;;) {
    spinTicks(ms(300));
    taskDelay(static_cast<_Vx_ticks_t>(ms(700)));
  }
}

// Load follows a two-minute sine wave between 0% and 40%.
int controlTask(_Vx_usr_arg_t /*unused*/) {
  constexpr double kPeriodSeconds = 120.0;
  const double rate = sysClkRateGet();
  for (;;) {
    const double t = static_cast<double>(tick64Get()) / rate;
    const double share = 0.2 + 0.2 * std::sin(2.0 * 3.14159265358979 * t / kPeriodSeconds);
    const auto busy = static_cast<unsigned>(share * 500.0);
    spinTicks(ms(busy));
    taskDelay(static_cast<_Vx_ticks_t>(ms(500U - busy)));
  }
}

// Allocates and frees memory in a slow cycle (up to 64 blocks of 128 KB).
int cacheTask(_Vx_usr_arg_t /*unused*/) {
  constexpr int kBlocks = 64;
  constexpr std::size_t kBlockSize = 128 * 1024;
  void* blocks[kBlocks] = {};
  int held = 0;
  int step = 1;
  for (;;) {
    if (step > 0) {
      blocks[held] = std::malloc(kBlockSize);
      ++held;
    } else {
      --held;
      std::free(blocks[held]);
      blocks[held] = nullptr;
    }
    if (held == kBlocks || held == 0)
      step = -step;
    spinTicks(ms(20));
    taskDelay(static_cast<_Vx_ticks_t>(ms(1000)));
  }
}

// Light periodic work.
int loggerTask(_Vx_usr_arg_t /*unused*/) {
  for (;;) {
    spinTicks(ms(50));
    taskDelay(static_cast<_Vx_ticks_t>(ms(950)));
  }
}

void spawn(const char* name, int priority, int (*entry)(_Vx_usr_arg_t)) {
  taskSpawn(const_cast<char*>(name),
            priority,
            0,
            16 * 1024,
            vxexp::asFuncptr(entry),
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
}

}  // namespace

/// @brief Starts the demo tasks (tSensor, tControl, tCache, tLogger).
/// @return 0.
extern "C" int vxExporterDemoLoad() {
  spawn("tSensor", 120, sensorTask);
  spawn("tControl", 130, controlTask);
  spawn("tCache", 140, cacheTask);
  spawn("tLogger", 200, loggerTask);
  return 0;
}
