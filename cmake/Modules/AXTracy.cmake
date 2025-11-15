option(AX_ENABLE_TRACY "Enable Tracy profiler." OFF)
message(STATUS "Profiling with Tracy: ${AX_ENABLE_TRACY}")

if(AX_ENABLE_TRACY)
  find_package(Tracy REQUIRED)
endif()
