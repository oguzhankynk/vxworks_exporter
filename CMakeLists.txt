cmake_minimum_required(VERSION 3.25)

project(vxworks_exporter
  VERSION 0.1.0
  DESCRIPTION "Prometheus exporter for VxWorks 7"
  LANGUAGES CXX)

# PREPEND: when this project is a subproject, its own modules must win over
# same-named modules of the parent project.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
find_package(Doxygen QUIET)

option(VXEXP_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
if(PROJECT_IS_TOP_LEVEL AND DOXYGEN_FOUND)
  set(_vxexp_docs_default ON)
else()
  set(_vxexp_docs_default OFF)
endif()
option(VXEXP_BUILD_DOCS "Build the Doxygen API reference ('docs' / 'docs-check' targets)" ${_vxexp_docs_default})
option(VXEXP_ENABLE_CLANG_TIDY "Run clang-tidy on the core library while building" OFF)
option(VXEXP_ENABLE_CPPCHECK "Run cppcheck on the core library while building" OFF)
option(VXEXP_ENABLE_COVERAGE "Build the core library and its tests with --coverage" OFF)
option(VXEXP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(VXEXP_SANITIZER "" CACHE STRING
  "Semicolon-separated sanitizers to enable, e.g. 'address;undefined' or 'thread'")

# Exporter defaults, compiled into the kernel module (see include/vxworks_exporter/vxworks_exporter.h).
set(VXEXP_DEFAULT_PORT 9100 CACHE STRING "TCP port vxExporterStart() uses when given 0")
set(VXEXP_DEFAULT_PRIORITY 250 CACHE STRING "Priority of the exporter's server task")
set(VXEXP_MAX_TASKS 2048 CACHE STRING "Most tasks listed in one scrape")
set(VXEXP_CPU_TABLE_SLOTS 1024 CACHE STRING "Tasks whose CPU time can be tracked at once")

# VxWorks builds: test runner (QEMU) and VXEXP_VX_RTP/DKM.
if(VXWORKS)
  include(VxWorksSupport)
endif()

include(CompilerWarnings)
include(Sanitizers)
include(Coverage)
include(ClangTidy)
include(Cppcheck)
include(Doxygen)
include(Dependencies)

add_subdirectory(src)

if(VXEXP_VX_RTP AND VXEXP_BUILD_TESTS)
  add_subdirectory(tools)
endif()

if(VXEXP_BUILD_TESTS)
  enable_testing()
  if(NOT VXEXP_VX_DKM)
    vxexp_fetch_test_dependencies()
  endif()
  add_subdirectory(tests)
endif()

if(VXEXP_BUILD_DOCS)
  vxexp_add_doxygen_targets()
endif()

# The product is the kernel module; host and RTP builds only test the core.
if(VXEXP_VX_DKM)
  include(GNUInstallDirs)
  install(TARGETS vxworks_exporter RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES include/vxworks_exporter/vxworks_exporter.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vxworks_exporter)
endif()
