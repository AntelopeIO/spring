add_executable(trx_generator main.cpp trx_generator.cpp trx_provider.cpp)

target_include_directories(trx_generator PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(trx_generator PRIVATE eosio_chain fc Boost::program_options ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS})

add_executable(trx_generator_tests trx_generator_tests.cpp trx_provider.cpp trx_generator.cpp)
target_link_libraries(trx_generator_tests PRIVATE eosio_chain fc Boost::program_options Boost::included_unit_test_framework ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS})
target_include_directories(trx_generator_tests PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
add_np_test(NAME trx_generator_tests COMMAND trx_generator_tests)
