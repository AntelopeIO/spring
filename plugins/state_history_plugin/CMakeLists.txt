file(GLOB HEADERS "include/eosio/state_history_plugin/*.hpp")
add_library( state_history_plugin
             state_history_plugin.cpp
             ${HEADERS} )

target_link_libraries( state_history_plugin state_history chain_plugin eosio_chain appbase )
target_include_directories( state_history_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if( CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11 )
   target_compile_options( state_history_plugin PRIVATE "-fcoroutines" )
endif()
