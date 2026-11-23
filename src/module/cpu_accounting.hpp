#ifndef VXEXP_MODULE_CPU_ACCOUNTING_HPP
#define VXEXP_MODULE_CPU_ACCOUNTING_HPP

#include <vxWorks.h>

#include <taskLib.h>

#include <cstdint>

namespace vxexp::cpu {

/// @brief Installs the task switch and delete hooks and starts counting.
///
/// Calibrates the CPU's timestamp counter against the system clock first
/// (about 200 ms). Every context switch then adds the time since the previous
/// switch on that CPU to the task being switched out.
/// @return true on success; false if a hook cannot be installed.
bool start();

/// @brief Removes the hooks. Counters keep their last values until start().
void stop();

/// @brief Whether start() succeeded and stop() has not been called.
/// @return true while counting.
bool running() noexcept;

/// @brief CPU time a task has used since start().
/// @param task The task.
/// @return Seconds, or 0 if the task has not been switched out yet.
double taskSeconds(TASK_ID task);

/// @brief Tasks that could not be counted because the table was full.
/// @return Running total since start().
std::uint64_t overflows() noexcept;

/// @brief Seconds on the calibrated timestamp counter (monotonic).
/// @return Seconds since an arbitrary point; 0 before calibration.
double nowSeconds() noexcept;

}  // namespace vxexp::cpu

#endif  // VXEXP_MODULE_CPU_ACCOUNTING_HPP
