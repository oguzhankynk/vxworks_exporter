include_guard(GLOBAL)

## vxexp_enable_sanitizers(<target>)
#
# Applies the sanitizers listed in VXEXP_SANITIZER (semicolon-separated,
# e.g. "address;undefined" or "thread") to <target>. A no-op when the
# variable is empty. See the "asan"/"tsan" presets.
function(vxexp_enable_sanitizers target)
  if(NOT VXEXP_SANITIZER)
    return()
  endif()

  if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    message(WARNING "VXEXP_SANITIZER is set but ${CMAKE_CXX_COMPILER_ID} is not GCC/Clang; ignoring.")
    return()
  endif()

  if(VXWORKS)
    # The VxWorks SDK compiler rejects -fsanitize=address/thread and ships no
    # sanitizer runtime. UBSan still works in trap mode: a failed check
    # executes an illegal instruction, and the RTP dies with SIGILL, which
    # vxrun reports as a failure. (A full VxWorks installation adds RTP ASan
    # and kernel KASAN as build options; they need a kernel/VSB rebuild,
    # which the SDK cannot do.)
    if(NOT VXEXP_SANITIZER STREQUAL "undefined")
      message(FATAL_ERROR "VxWorks SDK builds support VXEXP_SANITIZER=undefined only (got '${VXEXP_SANITIZER}').")
    endif()
    set(flags -fsanitize=undefined -fsanitize-trap=undefined -fno-omit-frame-pointer)
    target_compile_options(${target} PUBLIC ${flags})
    target_link_options(${target} PUBLIC ${flags})
    return()
  endif()

  list(JOIN VXEXP_SANITIZER "," sanitizer_csv)
  set(flags -fsanitize=${sanitizer_csv} -fno-omit-frame-pointer)
  if(NOT "thread" IN_LIST VXEXP_SANITIZER)
    list(APPEND flags -fno-sanitize-recover=all)
  endif()

  target_compile_options(${target} PUBLIC ${flags})
  target_link_options(${target} PUBLIC ${flags})
endfunction()

## vxexp_apply_sanitizer_test_wrapper(<target>)
#
# ThreadSanitizer needs a specific virtual-memory layout and aborts with
# "unexpected memory mapping" under some kernels' default ASLR behavior
# (observed under WSL2; `setarch -R <binary>` reproduces and fixes it
# without root). When VXEXP_SANITIZER includes "thread" and `setarch` is
# available, this wraps <target>'s test invocation (ctest/gtest_discover_tests)
# with it via CROSSCOMPILING_EMULATOR. No-op otherwise.
function(vxexp_apply_sanitizer_test_wrapper target)
  if(NOT "thread" IN_LIST VXEXP_SANITIZER)
    return()
  endif()

  find_program(SETARCH_EXECUTABLE setarch)
  if(NOT SETARCH_EXECUTABLE)
    return()
  endif()

  set_target_properties(${target} PROPERTIES
    CROSSCOMPILING_EMULATOR "${SETARCH_EXECUTABLE};-R")
endfunction()
