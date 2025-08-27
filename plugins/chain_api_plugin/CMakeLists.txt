file(GLOB HEADERS "include/eosio/chain_api_plugin/*.hpp")
add_library( chain_api_plugin
             chain_api_plugin.cpp
             api_v2.cpp
             ${HEADERS} )

target_link_libraries( chain_api_plugin chain_plugin http_plugin appbase interfaces )
target_include_directories( chain_api_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
