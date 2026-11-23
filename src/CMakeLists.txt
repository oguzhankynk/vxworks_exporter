# ---------------------------------------------------------------------------
# Portable core: exposition-format writer, sinks, HTTP request routing and the
# format checker used by the tests. Built for every target, so the host build
# can test it under sanitizers and coverage.
# ---------------------------------------------------------------------------
add_library(vxexp_core STATIC
  core/sink.cpp
  core/metric_writer.cpp
  core/http_request.cpp
  core/exposition_check.cpp
)
add_library(vxexp::core ALIAS vxexp_core)
target_compile_features(vxexp_core PUBLIC cxx_std_17)
target_include_directories(vxexp_core PUBLIC
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/include")

vxexp_set_warnings(vxexp_core)
vxexp_enable_sanitizers(vxexp_core)
vxexp_enable_coverage(vxexp_core)
vxexp_enable_clang_tidy(vxexp_core)
vxexp_enable_cppcheck(vxexp_core)

if(NOT VXEXP_VX_DKM)
  return()
endif()

# ---------------------------------------------------------------------------
# Kernel module (DKM): collectors, CPU accounting, HTTP server, C API.
# vxexp_module is shared by the product (vxworks_exporter.out) and the
# self-test module.
# ---------------------------------------------------------------------------
add_library(vxexp_module STATIC
  module/cpu_accounting.cpp
  module/collectors.cpp
  module/http_server.cpp
  module/api.cpp
)
target_link_libraries(vxexp_module PUBLIC vxexp::core)
target_compile_definitions(vxexp_module PRIVATE
  VXEXP_DEFAULT_PORT=${VXEXP_DEFAULT_PORT}
  VXEXP_DEFAULT_PRIORITY=${VXEXP_DEFAULT_PRIORITY}
  VXEXP_MAX_TASKS=${VXEXP_MAX_TASKS}
  VXEXP_CPU_TABLE_SLOTS=${VXEXP_CPU_TABLE_SLOTS})
vxexp_set_warnings(vxexp_module)

add_executable(vxworks_exporter module/module_main.cpp)
target_link_libraries(vxworks_exporter PRIVATE vxexp_module)
vxexp_set_warnings(vxworks_exporter)

# Synthetic workload for the demo dashboard (demo/); not part of the product.
add_executable(vxexp_demo_load "${PROJECT_SOURCE_DIR}/demo/load/demo_load.cpp")
target_include_directories(vxexp_demo_load PRIVATE "${PROJECT_SOURCE_DIR}/src")
vxexp_set_warnings(vxexp_demo_load)
