if(EOSIO_COMPILE_TEST_CONTRACTS)
   add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/db_find_secondary_test.wasm"
                      COMMAND "${CDT_ROOT}/bin/eosio-wast2wasm" "${CMAKE_CURRENT_SOURCE_DIR}/db_find_secondary_test.wast" -o "${CMAKE_CURRENT_BINARY_DIR}/db_find_secondary_test.wasm"
                      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/db_find_secondary_test.wast")

   add_custom_target(gen_db_find_secondary_test_wasm ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/db_find_secondary_test.wasm")
else()
   configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/db_find_secondary_test.wasm ${CMAKE_CURRENT_BINARY_DIR}/db_find_secondary_test.wasm COPYONLY )
endif()
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/db_find_secondary_test.abi  ${CMAKE_CURRENT_BINARY_DIR}/db_find_secondary_test.abi  COPYONLY )
