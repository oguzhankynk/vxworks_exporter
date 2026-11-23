# Target-side helper for tools/vxrun/vxrun.py: spawns the program under test
# and reports its exit status on the console. Built for RTP builds only.
enable_language(C)

add_executable(vxrun_launcher vxrun/launcher.c)
set_target_properties(vxrun_launcher PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
  CROSSCOMPILING_EMULATOR "")
# Static, so the launcher works whatever shared libraries are on the target.
target_link_options(vxrun_launcher PRIVATE -static)
