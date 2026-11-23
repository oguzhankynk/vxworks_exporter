include_guard(GLOBAL)

# VxWorks-specific setup. Included from the top-level CMakeLists.txt only when
# building with cmake/toolchains/vxworks.cmake (its platform module sets
# VXWORKS). Host builds never see this file.
#
# What it does:
#   * Treats the VxWorks headers as system headers, so the project's strict
#     warning flags apply to the project's code only.
#   * Points ctest at tools/vxrun/vxrun.py, which boots VxWorks in QEMU and
#     runs each test binary there (CMAKE_CROSSCOMPILING_EMULATOR).
#   * Defines VXEXP_VX_RTP / VXEXP_VX_DKM for the rest of the build.

set(VXEXP_VX_RTP OFF)
set(VXEXP_VX_DKM OFF)
if(VX_TARGET_TYPE STREQUAL "DKM")
  set(VXEXP_VX_DKM ON)
  # Kernel modules are linked into the kernel's address space: no PIC and no
  # shared libraries.
  set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
  if(BUILD_SHARED_LIBS)
    message(FATAL_ERROR "BUILD_SHARED_LIBS is not supported for DKM builds.")
  endif()
else()
  set(VXEXP_VX_RTP ON)
  # wr-cc links RTPs against libc.so by default, and not every kernel image
  # carries the shared C library. Static programs run on any kernel with RTP
  # support; BUILD_SHARED_LIBS=ON keeps dynamic linking.
  if(NOT BUILD_SHARED_LIBS)
    add_link_options(-static)
  endif()
endif()

# wr-cc adds most sysroot header directories with -I, so the compiler treats
# them as project headers: -Werror builds then fail on warnings inside VxWorks
# headers, and the sysroot's own copies of third-party headers (GoogleTest,
# Boost, ...) shadow the versions this project fetches. Re-adding the same
# directories with -isystem makes them system directories (the compiler then
# drops the -I copy) and puts them after the project's own include paths.
foreach(_dir IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
  cmake_path(IS_PREFIX CMAKE_SYSROOT "${_dir}" NORMALIZE _in_sysroot)
  if(_in_sysroot)
    add_compile_options("SHELL:-isystem ${_dir}")
  endif()
endforeach()

# --- Running programs on the target (tests) ------------------------------------
# The kernel that vxrun boots. The VxWorks SDK ships a QEMU kernel; with a full
# VxWorks installation, point this at a VIP's kernel image built with RTP
# support, the loader, the cmd shell and ATA + dosFs (see README).
file(GLOB _vx_sdk_kernels "$ENV{WIND_HOME}/vxsdk/bsps/*/vxWorks")
list(SORT _vx_sdk_kernels)
list(POP_FRONT _vx_sdk_kernels _vx_default_kernel)
set(VXEXP_VX_KERNEL "${_vx_default_kernel}" CACHE FILEPATH
  "VxWorks kernel image that ctest boots in QEMU (default: the SDK's QEMU kernel)")
if(VXEXP_BUILD_TESTS AND NOT EXISTS "${VXEXP_VX_KERNEL}")
  message(WARNING
    "VXEXP_VX_KERNEL ('${VXEXP_VX_KERNEL}') does not exist; ctest cannot run tests on VxWorks. "
    "Set it to a bootable x86-64 VxWorks kernel image.")
endif()

# The toolchain restricts find_*() to the sysroot; Python runs on the host.
find_program(VXEXP_PYTHON3 NAMES python3 NO_CMAKE_FIND_ROOT_PATH REQUIRED)

# Extra vxrun options for kernels that need a different QEMU machine than the
# SDK's, e.g. "--machine;q35;--cpu;Broadwell-noTSX;--disk-if;nvme;--no-bootline"
# (see `vxrun.py --help`).
set(VXEXP_VXRUN_OPTIONS "" CACHE STRING "Extra options for tools/vxrun/vxrun.py (a CMake list)")

# Base command for running anything on the target; callers append the mode.
set(VXEXP_VXRUN
  "${VXEXP_PYTHON3}" "${PROJECT_SOURCE_DIR}/tools/vxrun/vxrun.py" --kernel "${VXEXP_VX_KERNEL}"
  ${VXEXP_VXRUN_OPTIONS})

# Built by tools/CMakeLists.txt (RTP builds only).
set(VXEXP_VXRUN_LAUNCHER "${CMAKE_BINARY_DIR}/tools/vxrun_launcher.vxe")

if(VXEXP_VX_RTP)
  # Every executable created after this point gets the emulator, so
  # add_test(COMMAND <target>) runs the target inside QEMU. Shared libraries
  # built here (BUILD_SHARED_LIBS=ON) are copied next to the program.
  set(CMAKE_CROSSCOMPILING_EMULATOR
    ${VXEXP_VXRUN}
    --launcher "${VXEXP_VXRUN_LAUNCHER}"
    --lib-dir "${CMAKE_BINARY_DIR}/src"
    --lib-dir "${CMAKE_BINARY_DIR}/lib"
    --)
endif()
