# ---------------------------------------------------------------------------
# Kernel module (DKM) build: self-test module and end-to-end test, both on
# VxWorks in QEMU.
# ---------------------------------------------------------------------------
if(VXEXP_VX_DKM)
  add_executable(vxexp_selftest dkm/selftest.cpp)
  target_link_libraries(vxexp_selftest PRIVATE vxexp_module)
  vxexp_set_warnings(vxexp_selftest)

  add_test(NAME vxexp_selftest
    COMMAND ${VXEXP_VXRUN} --timeout 180 --dkm --entry vxExporterSelfTest
            $<TARGET_FILE:vxexp_selftest>)

  # Boots VxWorks, starts the real module, scrapes it from the host twice and
  # checks the result (see tests/e2e/scrape_test.py).
  add_test(NAME vxexp_e2e_scrape
    COMMAND "${VXEXP_PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/scrape_test.py"
            --vxrun "${PROJECT_SOURCE_DIR}/tools/vxrun/vxrun.py"
            --kernel "${VXEXP_VX_KERNEL}"
            --module $<TARGET_FILE:vxworks_exporter>
            ${VXEXP_VXRUN_OPTIONS})
  set_tests_properties(vxexp_e2e_scrape PROPERTIES LABELS e2e)
  return()
endif()

# ---------------------------------------------------------------------------
# Host and RTP builds: GoogleTest suite for the portable core.
# ---------------------------------------------------------------------------
include(GoogleTest)

add_executable(vxexp_unit_tests
  unit/test_metric_writer.cpp
  unit/test_sink.cpp
  unit/test_http_request.cpp
  unit/test_exposition_check.cpp
)
target_link_libraries(vxexp_unit_tests PRIVATE vxexp::core GTest::gtest GTest::gtest_main)

vxexp_set_warnings(vxexp_unit_tests)
vxexp_enable_sanitizers(vxexp_unit_tests)
vxexp_enable_coverage(vxexp_unit_tests)
vxexp_apply_sanitizer_test_wrapper(vxexp_unit_tests)

if(VXEXP_VX_RTP)
  # One QEMU boot per test binary; GoogleTest's output still lists each case.
  add_test(NAME vxexp_unit_tests COMMAND vxexp_unit_tests)
else()
  gtest_discover_tests(vxexp_unit_tests)
endif()

vxexp_add_coverage_target()
if(TARGET coverage)
  add_dependencies(coverage vxexp_unit_tests)
endif()
