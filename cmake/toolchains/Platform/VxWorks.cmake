# Platform module for CMAKE_SYSTEM_NAME=VxWorks, loaded by CMake through
# cmake/toolchains/vxworks.cmake. Describes how wr-cc names and links things.

set(VXWORKS 1)
set(UNIX 1)
set(CMAKE_DL_LIBS "")

set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")
if(VX_TARGET_TYPE STREQUAL "DKM")
  set(CMAKE_EXECUTABLE_SUFFIX ".out")  # partially linked kernel module
else()
  set(CMAKE_EXECUTABLE_SUFFIX ".vxe")  # RTP executable
endif()

# Shared libraries (RTP only).
set(CMAKE_SHARED_LIBRARY_C_FLAGS "-fPIC")
set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-shared")
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG "-Wl,-rpath,")
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG_SEP ":")
set(CMAKE_SHARED_LIBRARY_RPATH_LINK_C_FLAG "-Wl,-rpath-link,")
set(CMAKE_SHARED_LIBRARY_SONAME_C_FLAG "-Wl,-soname,")
set(CMAKE_EXE_EXPORTS_C_FLAG "-Wl,--export-dynamic")
foreach(type SHARED_LIBRARY SHARED_MODULE EXE)
  set(CMAKE_${type}_LINK_STATIC_C_FLAGS "-Wl,-Bstatic")
  set(CMAKE_${type}_LINK_DYNAMIC_C_FLAGS "-Wl,-Bdynamic")
endforeach()

foreach(lang C CXX)
  set(CMAKE_${lang}_FLAGS_DEBUG_INIT "-g -O0")
  set(CMAKE_${lang}_FLAGS_RELEASE_INIT "-O2 -DNDEBUG")
  set(CMAKE_${lang}_FLAGS_RELWITHDEBINFO_INIT "-g -O2 -DNDEBUG")
  set(CMAKE_${lang}_FLAGS_MINSIZEREL_INIT "-Os -DNDEBUG")
endforeach()
