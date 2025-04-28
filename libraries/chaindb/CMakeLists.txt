# Defines ChainBase library target.
project( ChainBase )

set(CMAKE_THREAD_PREFER_PTHREAD TRUE) 
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)

file(GLOB HEADERS "include/chainbase/*.hpp")
add_library( chainbase src/chainbase.cpp src/pinnable_mapped_file.cpp ${HEADERS} )
target_link_libraries( chainbase PUBLIC Threads::Threads Boost::system )

if(TARGET Boost::asio)
  target_link_libraries( chainbase PUBLIC
                         Boost::headers Boost::interprocess Boost::chrono Boost::multi_index Boost::lexical_cast Boost::asio )
endif()

target_include_directories( chainbase PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"  )

enable_testing()
add_subdirectory( test )
add_subdirectory( benchmark )

if(CHAINBASE_INSTALL_COMPONENT)
   set(INSTALL_COMPONENT_ARGS COMPONENT ${CHAINBASE_INSTALL_COMPONENT} EXCLUDE_FROM_ALL)
endif()

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/chainbase
        DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR} ${INSTALL_COMPONENT_ARGS})

install(TARGETS chainbase
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} ${INSTALL_COMPONENT_ARGS}
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} ${INSTALL_COMPONENT_ARGS})
