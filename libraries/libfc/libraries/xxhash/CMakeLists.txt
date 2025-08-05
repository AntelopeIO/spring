add_library(xxhash xxHash/xxhash.c)
target_include_directories(xxhash PUBLIC .)
#this one is a little weird: xxhash.c will automatically set this, so to avoid duplicate define warns, only add it in again for library users
###XXX: alternatively maybe just treat the library header-only in the first place, since it's happy to be used that way
###     Would that mess up x86 dispatch though?
target_compile_definitions(xxhash INTERFACE -DXXH_STATIC_LINKING_ONLY=1)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64 OR CMAKE_SYSTEM_PROCESSOR STREQUAL amd64)
   target_sources(xxhash PRIVATE xxHash/xxh_x86dispatch.c)
endif()

# avoid conflict with potential system lib
set_target_properties(xxhash PROPERTIES PREFIX libspring)

install(TARGETS xxhash
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
)

install(FILES xxHash/xxhash.h DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/springxxhash/xxHash" COMPONENT dev EXCLUDE_FROM_ALL)