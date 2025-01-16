if(EOSIO_COMPILE_TEST_CONTRACTS)
   add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tightloop.wasm"
                      COMMAND "${CDT_ROOT}/bin/eosio-wast2wasm" "${CMAKE_CURRENT_SOURCE_DIR}/tightloop.wast" -o "${CMAKE_CURRENT_BINARY_DIR}/tightloop.wasm"
                      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tightloop.wast")
   add_custom_target(gen_tightloop_wasm ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/tightloop.wasm")
else()
   configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/tightloop.wasm ${CMAKE_CURRENT_BINARY_DIR}/tightloop.wasm COPYONLY )
endif()
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/tightloop.abi  ${CMAKE_CURRENT_BINARY_DIR}/tightloop.abi  COPYONLY )
