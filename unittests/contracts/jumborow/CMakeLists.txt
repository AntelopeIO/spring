if(EOSIO_COMPILE_TEST_CONTRACTS)
   add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/jumborow.wasm"
                      COMMAND "${CDT_ROOT}/bin/eosio-wast2wasm" "${CMAKE_CURRENT_SOURCE_DIR}/jumborow.wast" -o "${CMAKE_CURRENT_BINARY_DIR}/jumborow.wasm"
                      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/jumborow.wast")
   add_custom_target(gen_jumborow_wasm ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/jumborow.wasm")
else()
   configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/jumborow.wasm ${CMAKE_CURRENT_BINARY_DIR}/jumborow.wasm COPYONLY )
endif()
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/jumborow.abi  ${CMAKE_CURRENT_BINARY_DIR}/jumborow.abi  COPYONLY )
