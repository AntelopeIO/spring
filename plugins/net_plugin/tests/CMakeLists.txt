add_executable( test_net_plugin
        auto_bp_peering_unittest.cpp
        rate_limit_parse_unittest.cpp
        main.cpp
)
target_link_libraries( test_net_plugin net_plugin eosio_testing eosio_chain_wrap )
add_p_test(NAME test_net_plugin COMMAND plugins/net_plugin/tests/test_net_plugin)
