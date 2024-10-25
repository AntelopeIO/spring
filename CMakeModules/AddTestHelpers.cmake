function(setup_test_common)
   cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;COST;TIMEOUT" "")

   if(arg_COST)
      set_tests_properties("${arg_NAME}" PROPERTIES COST ${arg_COST})
   endif()
   if(arg_TIMEOUT)
      set_tests_properties("${arg_NAME}" PROPERTIES TIMEOUT ${arg_TIMEOUT})
   endif()

   if(NOT WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.27")
      set_tests_properties("${arg_NAME}" PROPERTIES TIMEOUT_SIGNAL_NAME "SIGQUIT" TIMEOUT_SIGNAL_GRACE_PERIOD 60.0)
   endif()
endfunction()

function(add_p_test)
   cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;COST;TIMEOUT" "COMMAND")

   add_test(NAME "${arg_NAME}" COMMAND ${arg_COMMAND} WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
   setup_test_common(${ARGV})
endfunction()

function(add_wasmspec_test)
   cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;COST;TIMEOUT" "COMMAND")

   add_test(NAME "${arg_NAME}" COMMAND ${arg_COMMAND} WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
   setup_test_common(${ARGV})
   set_property(TEST "${arg_NAME}" PROPERTY LABELS wasm_spec_tests)
endfunction()

function(add_np_test)
   cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;COST;TIMEOUT" "COMMAND")

   add_test(NAME "${arg_NAME}" COMMAND ${arg_COMMAND} WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
   setup_test_common(${ARGV})
   set_property(TEST "${arg_NAME}" PROPERTY LABELS nonparallelizable_tests)
endfunction()

function(add_lr_test)
   cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;COST;TIMEOUT" "COMMAND")

   add_test(NAME "${arg_NAME}" COMMAND ${arg_COMMAND} WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
   setup_test_common(${ARGV})
   set_property(TEST "${arg_NAME}" PROPERTY LABELS long_running_tests)
endfunction()