execute_process( COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_INSTALL_FULL_LIBDIR}/python3/dist-packages  ERROR_QUIET RESULT_VARIABLE ret)
if(ret EQUAL "0")
    execute_process( COMMAND ${CMAKE_COMMAND} -E create_symlink ../../../share/spring_testing/tests/TestHarness ${CMAKE_INSTALL_FULL_LIBDIR}/python3/dist-packages/TestHarness)
    execute_process( COMMAND ${CMAKE_COMMAND} -E create_symlink ../../bin ${CMAKE_INSTALL_FULL_DATAROOTDIR}/spring_testing/bin)
endif()
