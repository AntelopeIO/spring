add_executable( test_resmon_plugin test_resmon_plugin.cpp test_add_file_system.cpp test_threshold.cpp )
target_link_libraries( test_resmon_plugin resource_monitor_plugin Boost::included_unit_test_framework )
target_include_directories( test_resmon_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
add_p_test(NAME test_resmon_plugin COMMAND plugins/resource_monitor_plugin/test/test_resmon_plugin)
