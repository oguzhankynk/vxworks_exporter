#ifndef VXEXP_MODULE_FUNCPTR_HPP
#define VXEXP_MODULE_FUNCPTR_HPP

#include <vxWorks.h>

namespace vxexp {

/// @brief Converts a typed function to the untyped FUNCPTR that hook and
///        task APIs (taskSwitchHookAdd, taskSpawn, ...) take.
///
/// Going through `void (*)()` is the sanctioned way to say "this conversion
/// is intended": VxWorks calls the function with the arguments its API
/// documents, which match the typed signature.
/// @param fn The function.
/// @return The same address as a FUNCPTR.
template <typename Fn>
FUNCPTR asFuncptr(Fn* fn) noexcept {
  return reinterpret_cast<FUNCPTR>(reinterpret_cast<void (*)()>(fn));
}

}  // namespace vxexp

#endif  // VXEXP_MODULE_FUNCPTR_HPP
