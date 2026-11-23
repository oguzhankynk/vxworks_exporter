# CMake toolchain file for VxWorks 7 (x86-64, LLVM, wr-cc).
#
# Works with both:
#   * the VxWorks SDK:          WIND_HOME=<unpacked SDK directory>
#   * a full VxWorks install:   the environment from wrenv (WIND_HOME, wr-cc on
#                               PATH) plus WIND_CC_SYSROOT=<VSB directory>
#
# Inputs (cache variable, then environment):
#   VX_TARGET_TYPE    RTP (user-space .vxe, default) or DKM (kernel module .out)
#   VXWORKS_SYSROOT   VSB directory; defaults to $WIND_CC_SYSROOT, then to the
#                     SDK's $WIND_HOME/vxsdk/sysroot
#
# Example:
#   cmake -G Ninja -B build \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/vxworks.cmake -DVX_TARGET_TYPE=DKM

cmake_minimum_required(VERSION 3.25)

set(VX_TARGET_TYPE "RTP" CACHE STRING "VxWorks target type: RTP or DKM")
set_property(CACHE VX_TARGET_TYPE PROPERTY STRINGS RTP DKM)
if(NOT VX_TARGET_TYPE MATCHES "^(RTP|DKM)$")
  message(FATAL_ERROR "VX_TARGET_TYPE must be RTP or DKM, not '${VX_TARGET_TYPE}'.")
endif()

# --- Sysroot (VSB) -----------------------------------------------------------
if(NOT VXWORKS_SYSROOT)
  if(NOT "$ENV{WIND_CC_SYSROOT}" STREQUAL "")
    set(_vx_sysroot "$ENV{WIND_CC_SYSROOT}")
  elseif(NOT "$ENV{WIND_HOME}" STREQUAL "")
    set(_vx_sysroot "$ENV{WIND_HOME}/vxsdk/sysroot")
  else()
    message(FATAL_ERROR
      "Cannot find VxWorks. Set WIND_HOME to the VxWorks SDK directory, or, for a full "
      "VxWorks installation, set WIND_CC_SYSROOT (or -DVXWORKS_SYSROOT) to a VSB directory.")
  endif()
  set(VXWORKS_SYSROOT "${_vx_sysroot}" CACHE PATH "VxWorks sysroot (VSB directory)")
endif()
if(NOT EXISTS "${VXWORKS_SYSROOT}/h/config/vsbConfig.h")
  message(FATAL_ERROR "'${VXWORKS_SYSROOT}' is not a VxWorks sysroot/VSB (no h/config/vsbConfig.h).")
endif()

# try_compile() projects re-read this file; hand them the same choices.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES VX_TARGET_TYPE VXWORKS_SYSROOT)

# --- Target system ------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")  # Platform/VxWorks.cmake
set(CMAKE_SYSTEM_NAME VxWorks)
set(CMAKE_SYSTEM_VERSION 7)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSROOT "${VXWORKS_SYSROOT}")

# --- Compiler driver ------------------------------------------------------------
# SDK: <WIND_HOME>/vxsdk/host/x86_64-linux/bin. Full install: wrenv puts wr-cc
# on PATH.
find_program(VX_WRCC wr-cc
  HINTS "$ENV{WIND_HOME}/vxsdk/host/x86_64-linux/bin"
  NO_CMAKE_FIND_ROOT_PATH)
if(NOT VX_WRCC)
  message(FATAL_ERROR
    "wr-cc not found. Set WIND_HOME to the VxWorks SDK directory, or run cmake from a "
    "wrenv shell of a full VxWorks installation.")
endif()
get_filename_component(_vx_bin "${VX_WRCC}" DIRECTORY)
set(CMAKE_C_COMPILER "${_vx_bin}/wr-cc")
set(CMAKE_CXX_COMPILER "${_vx_bin}/wr-c++")
set(CMAKE_ASM_COMPILER "${_vx_bin}/wr-cc")
foreach(_tool ar nm ranlib objcopy objdump readelf strip size)
  string(TOUPPER "${_tool}" _var)
  set(CMAKE_${_var} "${_vx_bin}/wr-${_tool}" CACHE FILEPATH "" FORCE)
endforeach()
set(CMAKE_LINKER "${_vx_bin}/wr-ld" CACHE FILEPATH "" FORCE)

# --- Target type ------------------------------------------------------------------
if(VX_TARGET_TYPE STREQUAL "DKM")
  foreach(_lang C CXX ASM)
    set(CMAKE_${_lang}_FLAGS_INIT "-dkm")
  endforeach()
  # A kernel module is partially linked and has no main(): test the compiler
  # by building a static library instead of an executable.
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  set(_vx_prefix /krnl/3pp/develop/krnl)
  set(_vx_include /krnl/h/published/UTILS_UNIX /share/h /krnl/h/system /krnl/h/public)
  set(_vx_lib /krnl/llvm)
else()
  set(_vx_prefix /usr/3pp/develop/usr)
  set(_vx_include /usr/h/published/UTILS_UNIX /share/h /usr/h /usr/h/system /usr/h/public)
  set(_vx_lib /usr/lib/common)
endif()

# find_*() look in the sysroot for target files and on the host for programs.
set(CMAKE_SYSTEM_PREFIX_PATH ${_vx_prefix})
set(CMAKE_SYSTEM_INCLUDE_PATH ${_vx_include})
set(CMAKE_SYSTEM_LIBRARY_PATH ${_vx_lib})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
