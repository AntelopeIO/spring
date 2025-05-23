file(GLOB HEADERS "include/eosio/chain_plugin/*.hpp")
add_library( chain_plugin
             account_query_db.cpp
             trx_finality_status_processing.cpp
             chain_plugin.cpp
             trx_retry_db.cpp
             tracked_votes.cpp
             get_info_db.cpp
             ${HEADERS} )

if(EOSIO_ENABLE_DEVELOPER_OPTIONS)
    message(WARNING "EOSIO Developer Options are enabled; these are NOT supported")
    target_compile_definitions(chain_plugin PUBLIC EOSIO_DEVELOPER)
endif()

target_link_libraries( chain_plugin eosio_chain custom_appbase appbase resource_monitor_plugin Boost::bimap )
target_include_directories( chain_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../chain_interface/include" "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/appbase/include" "${CMAKE_CURRENT_SOURCE_DIR}/../resource_monitor_plugin/include")

add_subdirectory( test )
