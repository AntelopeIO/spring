file(GLOB HEADERS "include/eosio/state-history/*.hpp")

add_library( state_history
             abi.cpp
             create_deltas.cpp
             trace_converter.cpp
             ${HEADERS}
           )

target_link_libraries( state_history 
                       PUBLIC eosio_chain fc chainbase softfloat
                     )

target_include_directories( state_history
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../wasm-jit/Include"
                          )
