// Per-task CPU time from a task switch hook.
//
// VxWorks has no public per-task CPU time API (spy only prints), so the
// exporter counts it: at every context switch, the time since the previous
// switch on that CPU goes to the task being switched out.
//
// Clock: the x86-64 timestamp counter. The SDK kernel has no 64-bit
// sysTimestamp64(), and the 32-bit vxbTimestamp() wraps within seconds, which
// would lose time for a task that runs longer than one wrap. The TSC must be
// invariant and synchronised across cores (true for current x86-64 CPUs and
// QEMU). Its rate is measured against the system clock at start().
//
// Interrupt time is charged to the task that was interrupted, as with any
// switch-hook accounting.

#include "module/cpu_accounting.hpp"

#include <spinLockLib.h>
#include <sysLib.h>
#include <taskHookLib.h>
#include <tickLib.h>
#include <vxCpuLib.h>
#include <x86intrin.h>

#include <atomic>
#include <cstddef>

#include "module/funcptr.hpp"

#ifndef VXEXP_CPU_TABLE_SLOTS
#define VXEXP_CPU_TABLE_SLOTS 1024
#endif

#ifndef VX_MAX_SMP_CPUS
#define VX_MAX_SMP_CPUS 64
#endif

namespace vxexp::cpu {

namespace {

constexpr std::size_t kSlots = VXEXP_CPU_TABLE_SLOTS;
constexpr std::size_t kMaxCpus = VX_MAX_SMP_CPUS;

// A slot whose task was deleted; lookups continue past it.
const TASK_ID kTombstone = reinterpret_cast<TASK_ID>(static_cast<uintptr_t>(1));

struct Slot {
  TASK_ID task;
  std::uint64_t cycles;
};

Slot table[kSlots];
std::uint64_t last_switch[kMaxCpus];
spinlockIsr_t lock;
std::uint64_t overflow_count;
double cycles_per_second;
std::atomic<bool> is_running{false};

inline std::uint64_t cycles() noexcept {
  return __rdtsc();
}

std::size_t home(TASK_ID task) noexcept {
  // TASK_IDs are aligned pointers or handles; drop the low bits.
  return static_cast<std::size_t>((reinterpret_cast<uintptr_t>(task) >> 4) % kSlots);
}

// Caller holds the lock.
Slot* find(TASK_ID task, bool insert) noexcept {
  Slot* free_slot = nullptr;
  const std::size_t start = home(task);
  for (std::size_t i = 0; i < kSlots; ++i) {
    Slot& slot = table[(start + i) % kSlots];
    if (slot.task == task) {
      return &slot;
    }
    if (slot.task == kTombstone && free_slot == nullptr) {
      free_slot = &slot;
    }
    if (slot.task == nullptr) {
      if (free_slot == nullptr) {
        free_slot = &slot;
      }
      break;
    }
  }
  if (!insert) {
    return nullptr;
  }
  if (free_slot != nullptr) {
    free_slot->task = task;
    free_slot->cycles = 0;
  }
  return free_slot;
}

// Runs at every context switch, with the switching CPU's scheduler locked:
// no allocation, no blocking, nothing but arithmetic under a spinlock.
void switchHook(WIND_TCB* old_tcb, WIND_TCB* /*new_tcb*/) {
  const std::uint64_t now = cycles();
  const unsigned int cpu = vxCpuIndexGet();
  SPIN_LOCK_ISR_TAKE(&lock);
  if (cpu < kMaxCpus) {
    const std::uint64_t elapsed = now - last_switch[cpu];
    last_switch[cpu] = now;
    Slot* slot = find(taskWindTcbToTaskId(old_tcb), true);
    if (slot != nullptr) {
      slot->cycles += elapsed;
    } else {
      ++overflow_count;
    }
  }
  SPIN_LOCK_ISR_GIVE(&lock);
}

void deleteHook(WIND_TCB* tcb) {
  const TASK_ID task = taskWindTcbToTaskId(tcb);
  SPIN_LOCK_ISR_TAKE(&lock);
  Slot* slot = find(task, false);
  if (slot != nullptr) {
    slot->task = kTombstone;
    slot->cycles = 0;
  }
  SPIN_LOCK_ISR_GIVE(&lock);
}

void calibrate() {
  const auto rate = static_cast<_Vx_ticks64_t>(sysClkRateGet());
  const _Vx_ticks64_t window = rate > 5 ? rate / 5 : 1;
  // Start on a tick edge so the window is whole ticks.
  const _Vx_ticks64_t edge = tick64Get();
  while (tick64Get() == edge) {
    taskDelay(0);
  }
  const _Vx_ticks64_t t0 = tick64Get();
  const std::uint64_t c0 = cycles();
  taskDelay(static_cast<_Vx_ticks_t>(window));
  while (tick64Get() < t0 + window) {
    taskDelay(0);
  }
  const std::uint64_t c1 = cycles();
  const _Vx_ticks64_t t1 = tick64Get();
  cycles_per_second =
      static_cast<double>(c1 - c0) * static_cast<double>(rate) / static_cast<double>(t1 - t0);
}

}  // namespace

bool start() {
  if (is_running.load()) {
    return true;
  }
  SPIN_LOCK_ISR_INIT(&lock, 0);
  if (cycles_per_second <= 0.0) {
    calibrate();
  }
  for (Slot& slot : table) {
    slot = Slot{nullptr, 0};
  }
  overflow_count = 0;
  const std::uint64_t now = cycles();
  for (std::uint64_t& t : last_switch) {
    t = now;
  }
  if (taskDeleteHookAdd(asFuncptr(deleteHook)) != OK) {
    return false;
  }
  if (taskSwitchHookAdd(asFuncptr(switchHook)) != OK) {
    taskDeleteHookDelete(asFuncptr(deleteHook));
    return false;
  }
  is_running.store(true);
  return true;
}

void stop() {
  if (!is_running.exchange(false)) {
    return;
  }
  taskSwitchHookDelete(asFuncptr(switchHook));
  taskDeleteHookDelete(asFuncptr(deleteHook));
}

bool running() noexcept {
  return is_running.load();
}

double taskSeconds(TASK_ID task) {
  if (cycles_per_second <= 0.0) {
    return 0.0;
  }
  std::uint64_t value = 0;
  SPIN_LOCK_ISR_TAKE(&lock);
  const Slot* slot = find(task, false);
  if (slot != nullptr) {
    value = slot->cycles;
  }
  SPIN_LOCK_ISR_GIVE(&lock);
  return static_cast<double>(value) / cycles_per_second;
}

std::uint64_t overflows() noexcept {
  return overflow_count;
}

double nowSeconds() noexcept {
  return cycles_per_second > 0.0 ? static_cast<double>(cycles()) / cycles_per_second : 0.0;
}

}  // namespace vxexp::cpu
