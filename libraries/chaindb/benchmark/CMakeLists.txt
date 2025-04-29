file(GLOB UNIT_TESTS "bench.cpp")
add_executable( chainbase_bench EXCLUDE_FROM_ALL bench.cpp  )
target_link_libraries( chainbase_bench  chainbase ${PLATFORM_SPECIFIC_LIBS} )
