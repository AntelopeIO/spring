add_subdirectory(bssl EXCLUDE_FROM_ALL)
target_compile_options(fipsmodule PRIVATE -Wno-error)
target_compile_options(crypto PRIVATE -Wno-error)
target_compile_options(decrepit PRIVATE -Wno-error)

# boringssl's forced-on debug symbols interfere with reproducibility due to path differences; disable on Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
   target_compile_options(fipsmodule PRIVATE -g0)
   target_compile_options(crypto PRIVATE -g0)
   target_compile_options(decrepit PRIVATE -g0)
endif()

#paranoia for when a dependent library depends on openssl (such as libcurl)
set_target_properties(fipsmodule PROPERTIES C_VISIBILITY_PRESET hidden)
set_target_properties(crypto PROPERTIES C_VISIBILITY_PRESET hidden)
set_target_properties(decrepit PROPERTIES C_VISIBILITY_PRESET hidden)

add_library(boringssl INTERFACE)
target_link_libraries(boringssl INTERFACE crypto decrepit)
target_include_directories(boringssl INTERFACE boringssl/src/include)

# avoid conflict with system lib
set_target_properties(crypto PROPERTIES PREFIX libbs)

install( TARGETS crypto 
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
)

install( TARGETS decrepit 
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
)

install( DIRECTORY bssl/include/ DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/springboringssl" COMPONENT dev EXCLUDE_FROM_ALL )
