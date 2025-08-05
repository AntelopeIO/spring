add_library(xxhash INTERFACE)
target_include_directories(xxhash INTERFACE .)
target_compile_definitions(xxhash INTERFACE -DXXH_INLINE_ALL=1)

install(FILES xxHash/xxhash.h DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/springxxhash/xxHash" COMPONENT dev EXCLUDE_FROM_ALL)