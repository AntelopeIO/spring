include_directories( "${CMAKE_SOURCE_DIR}/plugins/wallet_plugin/include" )

file(GLOB UNIT_TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cpp")
list(REMOVE_ITEM UNIT_TESTS ship_client.cpp)
list(REMOVE_ITEM UNIT_TESTS ship_streamer.cpp)

add_executable( plugin_test ${UNIT_TESTS} )
target_link_libraries( plugin_test eosio_testing eosio_chain_wrap chainbase chain_plugin producer_plugin wallet_plugin fc state_history Boost::included_unit_test_framework ${PLATFORM_SPECIFIC_LIBS} )

target_include_directories( plugin_test PUBLIC
                            ${CMAKE_SOURCE_DIR}/plugins/net_plugin/include
                            ${CMAKE_SOURCE_DIR}/plugins/chain_plugin/include
                            ${CMAKE_SOURCE_DIR}/unittests
                            ${CMAKE_BINARY_DIR}/unittests/include/ )

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/p2p_tests/dawn_515/test.sh ${CMAKE_CURRENT_BINARY_DIR}/p2p_tests/dawn_515/test.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/block_log_util_test.py ${CMAKE_CURRENT_BINARY_DIR}/block_log_util_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/block_log_retain_blocks_test.py ${CMAKE_CURRENT_BINARY_DIR}/block_log_retain_blocks_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/bridge_for_fork_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/bridge_for_fork_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cluster_launcher.py ${CMAKE_CURRENT_BINARY_DIR}/cluster_launcher.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/distributed-transactions-test.py ${CMAKE_CURRENT_BINARY_DIR}/distributed-transactions-test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/spring_util_bls_test.py ${CMAKE_CURRENT_BINARY_DIR}/spring_util_bls_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/spring_util_snapshot_info_test.py ${CMAKE_CURRENT_BINARY_DIR}/spring_util_snapshot_info_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/sample-cluster-map.json ${CMAKE_CURRENT_BINARY_DIR}/sample-cluster-map.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/restart-scenarios-test.py ${CMAKE_CURRENT_BINARY_DIR}/restart-scenarios-test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/terminate-scenarios-test.py ${CMAKE_CURRENT_BINARY_DIR}/terminate-scenarios-test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/terminate_scenarios_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/terminate_scenarios_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/liveness_test.py ${CMAKE_CURRENT_BINARY_DIR}/liveness_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_signal_throw_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_signal_throw_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_startup_catchup.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_startup_catchup.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_snapshot_diff_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_snapshot_diff_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_snapshot_forked_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_snapshot_forked_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/bridge_consecutive_shape.json ${CMAKE_CURRENT_BINARY_DIR}/bridge_consecutive_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_forked_chain_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_forked_chain_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_short_fork_take_over_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_short_fork_take_over_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_run_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_run_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_lib_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_lib_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_under_min_avail_ram.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_under_min_avail_ram.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_voting_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_voting_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_irreversible_mode_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_irreversible_mode_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_read_terminate_at_block_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_read_terminate_at_block_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_chainbase_allocation_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_chainbase_allocation_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_protocol_feature_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_protocol_feature_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_multiple_version_protocol_feature_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_multiple_version_protocol_feature_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_extra_packed_data_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_extra_packed_data_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_late_block_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_late_block_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_late_block_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/nodeos_late_block_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/validate-dirty-db.py ${CMAKE_CURRENT_BINARY_DIR}/validate-dirty-db.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/keosd_auto_launch_test.py ${CMAKE_CURRENT_BINARY_DIR}/keosd_auto_launch_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/db_modes_test.sh ${CMAKE_CURRENT_BINARY_DIR}/db_modes_test.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/prod_preactivation_test.py ${CMAKE_CURRENT_BINARY_DIR}/prod_preactivation_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/release-build.sh ${CMAKE_CURRENT_BINARY_DIR}/release-build.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/separate_prod_fin_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/separate_prod_fin_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/separate_prod_fin_test.py ${CMAKE_CURRENT_BINARY_DIR}/separate_prod_fin_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/snapshot_in_svnn_transition_test.py ${CMAKE_CURRENT_BINARY_DIR}/snapshot_in_svnn_transition_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version-label.sh ${CMAKE_CURRENT_BINARY_DIR}/version-label.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/full-version-label.sh ${CMAKE_CURRENT_BINARY_DIR}/full-version-label.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_producer_watermark_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_producer_watermark_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cli_test.py ${CMAKE_CURRENT_BINARY_DIR}/cli_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ship_reqs_across_svnn_test.py ${CMAKE_CURRENT_BINARY_DIR}/ship_reqs_across_svnn_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ship_restart_test.py ${CMAKE_CURRENT_BINARY_DIR}/ship_restart_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ship_test.py ${CMAKE_CURRENT_BINARY_DIR}/ship_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ship_streamer_test.py ${CMAKE_CURRENT_BINARY_DIR}/ship_streamer_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ship_kill_client_test.py ${CMAKE_CURRENT_BINARY_DIR}/ship_kill_client_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/bridge_for_fork_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/bridge_for_fork_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/lib_advance_test.py ${CMAKE_CURRENT_BINARY_DIR}/lib_advance_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/http_plugin_test.py ${CMAKE_CURRENT_BINARY_DIR}/http_plugin_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/p2p_high_latency_test.py ${CMAKE_CURRENT_BINARY_DIR}/p2p_high_latency_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/p2p_multiple_listen_test.py ${CMAKE_CURRENT_BINARY_DIR}/p2p_multiple_listen_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/p2p_no_listen_test.py ${CMAKE_CURRENT_BINARY_DIR}/p2p_no_listen_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/p2p_sync_throttle_test.py ${CMAKE_CURRENT_BINARY_DIR}/p2p_sync_throttle_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/p2p_sync_throttle_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/p2p_sync_throttle_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/p2p_no_blocks_test.py ${CMAKE_CURRENT_BINARY_DIR}/p2p_no_blocks_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/p2p_no_blocks_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/p2p_no_blocks_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/compute_transaction_test.py ${CMAKE_CURRENT_BINARY_DIR}/compute_transaction_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/subjective_billing_test.py ${CMAKE_CURRENT_BINARY_DIR}/subjective_billing_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/get_account_test.py ${CMAKE_CURRENT_BINARY_DIR}/get_account_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_high_transaction_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_high_transaction_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_retry_transaction_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_retry_transaction_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/transition_to_if.py ${CMAKE_CURRENT_BINARY_DIR}/transition_to_if.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/disaster_recovery.py ${CMAKE_CURRENT_BINARY_DIR}/disaster_recovery.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/disaster_recovery_2.py ${CMAKE_CURRENT_BINARY_DIR}/disaster_recovery_2.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/disaster_recovery_2_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/disaster_recovery_2_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/disaster_recovery_3.py ${CMAKE_CURRENT_BINARY_DIR}/disaster_recovery_3.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/production_pause_max_rev_blks_test.py ${CMAKE_CURRENT_BINARY_DIR}/production_pause_max_rev_blks_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/production_pause_max_rev_blks_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/production_pause_max_rev_blks_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/production_pause_vote_timeout.py ${CMAKE_CURRENT_BINARY_DIR}/production_pause_vote_timeout.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/production_pause_vote_timeout_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/production_pause_vote_timeout_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/production_restart.py ${CMAKE_CURRENT_BINARY_DIR}/production_restart.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/production_restart_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/production_restart_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/pause_at_block_test.py ${CMAKE_CURRENT_BINARY_DIR}/pause_at_block_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/trx_finality_status_test.py ${CMAKE_CURRENT_BINARY_DIR}/trx_finality_status_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/trx_finality_status_forked_test.py ${CMAKE_CURRENT_BINARY_DIR}/trx_finality_status_forked_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/plugin_http_api_test.py ${CMAKE_CURRENT_BINARY_DIR}/plugin_http_api_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/plugin_http_api_test_savanna.py ${CMAKE_CURRENT_BINARY_DIR}/plugin_http_api_test_savanna.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_contrl_c_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_contrl_c_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/read_only_trx_test.py ${CMAKE_CURRENT_BINARY_DIR}/read_only_trx_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/interrupt_read_only_trx_test.py ${CMAKE_CURRENT_BINARY_DIR}/interrupt_read_only_trx_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/resource_monitor_plugin_test.py ${CMAKE_CURRENT_BINARY_DIR}/resource_monitor_plugin_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/light_validation_sync_test.py ${CMAKE_CURRENT_BINARY_DIR}/light_validation_sync_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/interrupt_trx_test.py ${CMAKE_CURRENT_BINARY_DIR}/interrupt_trx_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/trace_plugin_test.py ${CMAKE_CURRENT_BINARY_DIR}/trace_plugin_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nested_container_multi_index_test.py ${CMAKE_CURRENT_BINARY_DIR}/nested_container_multi_index_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/auto_bp_peering_test.py ${CMAKE_CURRENT_BINARY_DIR}/auto_bp_peering_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/auto_bp_peering_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/auto_bp_peering_test_shape.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/auto_bp_gossip_peering_test.py ${CMAKE_CURRENT_BINARY_DIR}/auto_bp_gossip_peering_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/gelf_test.py ${CMAKE_CURRENT_BINARY_DIR}/gelf_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/split_blocklog_replay_test.py ${CMAKE_CURRENT_BINARY_DIR}/split_blocklog_replay_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/PerformanceHarnessScenarioRunner.py ${CMAKE_CURRENT_BINARY_DIR}/PerformanceHarnessScenarioRunner.py COPYONLY)

if(DEFINED ENV{GITHUB_ACTIONS})
  set(UNSHARE "--unshared")
else()
  set(UNSHARE "")
endif()

option(SPRING_ENABLE_RELEASE_BUILD_TEST "Enables a test that verifies nodeos was compiled with compiler options typical for a release build" On)
if(SPRING_ENABLE_RELEASE_BUILD_TEST)
  add_p_test(NAME release-build-test COMMAND tests/release-build.sh)
endif()

#To run plugin_test with all log from blockchain displayed, put --verbose after --, i.e. plugin_test -- --verbose
add_p_test(NAME plugin_test COMMAND plugin_test --report_level=detailed --color_output)

add_np_test(NAME nodeos_sanity_test COMMAND tests/nodeos_run_test.py -v --sanity-test ${UNSHARE})
add_np_test(NAME nodeos_run_test COMMAND tests/nodeos_run_test.py -v ${UNSHARE})
add_np_test(NAME nodeos_run_if_test COMMAND tests/nodeos_run_test.py -v --activate-if ${UNSHARE})
add_np_test(NAME nodeos_lib_test COMMAND tests/nodeos_lib_test.py -n 4 -p 3 -s ring -v ${UNSHARE})
add_np_test(NAME nodeos_lib_if_test COMMAND tests/nodeos_lib_test.py -n 4 -p 3 -s ring -v --activate-if ${UNSHARE})
add_np_test(NAME block_log_util_test COMMAND tests/block_log_util_test.py -v ${UNSHARE})
add_np_test(NAME block_log_util_if_test COMMAND tests/block_log_util_test.py --activate-if -v ${UNSHARE})
add_np_test(NAME block_log_retain_blocks_test COMMAND tests/block_log_retain_blocks_test.py -v ${UNSHARE})
add_np_test(NAME block_log_retain_blocks_if_test COMMAND tests/block_log_retain_blocks_test.py --activate-if -v ${UNSHARE})

add_subdirectory( TestHarness )
add_subdirectory( trx_generator )
add_subdirectory( PerformanceHarness )

find_package(Threads)
add_executable(ship_client ship_client.cpp)
target_link_libraries(ship_client eosio_chain Boost::program_options Boost::system Boost::algorithm Boost::asio Boost::beast Threads::Threads)
add_executable(ship_streamer ship_streamer.cpp)
target_link_libraries(ship_streamer eosio_chain Boost::program_options Boost::system Boost::asio Boost::beast Threads::Threads)

add_np_test(NAME cluster_launcher COMMAND tests/cluster_launcher.py -v ${UNSHARE})
add_np_test(NAME cluster_launcher_if COMMAND tests/cluster_launcher.py --activate-if -v ${UNSHARE})

add_np_test(NAME transition_to_if COMMAND tests/transition_to_if.py -v ${UNSHARE})
add_lr_test(NAME transition_to_if_lr COMMAND tests/transition_to_if.py -v -p 20 ${UNSHARE})

add_np_test(NAME disaster_recovery COMMAND tests/disaster_recovery.py -v ${UNSHARE})
add_np_test(NAME disaster_recovery_2 COMMAND tests/disaster_recovery_2.py -v ${UNSHARE})
add_np_test(NAME disaster_recovery_3 COMMAND tests/disaster_recovery_3.py -v ${UNSHARE})
add_np_test(NAME production_pause_max_rev_blks_test COMMAND tests/production_pause_max_rev_blks_test.py -v ${UNSHARE})
add_np_test(NAME production_pause_vote_timeout COMMAND tests/production_pause_vote_timeout.py -v ${UNSHARE})
add_np_test(NAME production_restart COMMAND tests/production_restart.py -v ${UNSHARE})

add_np_test(NAME ship_reqs_across_svnn_test COMMAND tests/ship_reqs_across_svnn_test.py -v ${UNSHARE})
add_np_test(NAME ship_restart_test COMMAND tests/ship_restart_test.py -v ${UNSHARE})
add_np_test(NAME ship_test COMMAND tests/ship_test.py -v --num-clients 10 --num-requests 5000 ${UNSHARE})
add_np_test(NAME ship_test_unix COMMAND tests/ship_test.py -v --num-clients 10 --num-requests 5000 ${UNSHARE} --unix-socket)
add_np_test(NAME ship_if_test COMMAND tests/ship_test.py -v --activate-if --num-clients 10 --num-requests 5000 ${UNSHARE})

add_lr_test(NAME ship_streamer_test COMMAND tests/ship_streamer_test.py -v --num-clients 10 ${UNSHARE})
add_lr_test(NAME ship_streamer_if_test COMMAND tests/ship_streamer_test.py -v --num-clients 10 --activate-if ${UNSHARE})
add_lr_test(NAME ship_streamer_if_fetch_finality_data_test COMMAND tests/ship_streamer_test.py -v --num-clients 10 --activate-if --finality-data-history ${UNSHARE})
add_np_test(NAME ship_kill_client_test COMMAND tests/ship_kill_client_test.py -v --num-clients 20 ${UNSHARE})

add_np_test(NAME p2p_dawn515_test COMMAND tests/p2p_tests/dawn_515/test.sh)

add_np_test(NAME producer-preactivate-feature-test COMMAND tests/prod_preactivation_test.py ${UNSHARE})
add_np_test(NAME separate_prod_fin_test COMMAND tests/separate_prod_fin_test.py ${UNSHARE})
add_np_test(NAME snapshot_in_svnn_transition_test COMMAND tests/snapshot_in_svnn_transition_test.py ${UNSHARE})
add_np_test(NAME nodeos_protocol_feature_test COMMAND tests/nodeos_protocol_feature_test.py -v ${UNSHARE})
add_np_test(NAME compute_transaction_test COMMAND tests/compute_transaction_test.py -v -p 2 -n 3 ${UNSHARE})
add_np_test(NAME read-only-trx-basic-test COMMAND tests/read_only_trx_test.py -p 2 -n 3 --read-only-threads 1 ${UNSHARE})
add_np_test(NAME read-only-trx-parallel-test COMMAND tests/read_only_trx_test.py -p 2 -n 3 --read-only-threads 16 --num-test-runs 2 ${UNSHARE})
add_np_test(NAME read-only-trx-basic-if-test COMMAND tests/read_only_trx_test.py -p 2 -n 3 --read-only-threads 1 --activate-if ${UNSHARE})
add_np_test(NAME read-only-trx-parallel-if-test COMMAND tests/read_only_trx_test.py -p 2 -n 3 --read-only-threads 16 --num-test-runs 2 --activate-if ${UNSHARE})
add_np_test(NAME read-only-trx-parallel-if-eos-vm-oc-test COMMAND tests/read_only_trx_test.py -p 2 -n 3 --eos-vm-oc-enable all --read-only-threads 16 --num-test-runs 2 --activate-if ${UNSHARE})
add_np_test(NAME read-only-trx-parallel-no-oc-if-test COMMAND tests/read_only_trx_test.py -p 2 -n 3 --eos-vm-oc-enable none --read-only-threads 6 --num-test-runs 2 --activate-if ${UNSHARE})
add_np_test(NAME interrupt-read-only-trx-basic-test COMMAND tests/interrupt_read_only_trx_test.py -p 2 -n 3 --read-only-threads 1 ${UNSHARE})
add_np_test(NAME interrupt-read-only-trx-parallel-test COMMAND tests/interrupt_read_only_trx_test.py -p 2 -n 3 --read-only-threads 16 ${UNSHARE})
add_np_test(NAME interrupt-read-only-trx-parallel-if-eos-vm-oc-test COMMAND tests/interrupt_read_only_trx_test.py -p 2 -n 3 --eos-vm-oc-enable all --read-only-threads 16 ${UNSHARE})
add_np_test(NAME subjective_billing_test COMMAND tests/subjective_billing_test.py -v -p 2 -n 4 ${UNSHARE})
add_np_test(NAME get_account_test COMMAND tests/get_account_test.py -v -p 2 -n 3 ${UNSHARE})
add_np_test(NAME pause_at_block_test COMMAND tests/pause_at_block_test.py -v ${UNSHARE})

add_np_test(NAME distributed-transactions-test COMMAND tests/distributed-transactions-test.py -d 2 -p 4 -n 6 -v ${UNSHARE})
add_np_test(NAME distributed-transactions-if-test COMMAND tests/distributed-transactions-test.py -d 2 -p 4 -n 6 --activate-if -v ${UNSHARE})
add_np_test(NAME restart-scenarios-test-resync COMMAND tests/restart-scenarios-test.py -c resync -p4 -v ${UNSHARE})
add_np_test(NAME restart-scenarios-if-test-resync COMMAND tests/restart-scenarios-test.py -c resync -p4 -v --activate-if ${UNSHARE})
add_np_test(NAME restart-scenarios-test-hard_replay COMMAND tests/restart-scenarios-test.py -c hardReplay -p4 -v ${UNSHARE})
add_np_test(NAME restart-scenarios-if-test-hard_replay COMMAND tests/restart-scenarios-test.py -c hardReplay -p4 -v --activate-if ${UNSHARE})
add_np_test(NAME restart-scenarios-test-none COMMAND tests/restart-scenarios-test.py -c none --kill-sig term -p4 -v ${UNSHARE})
add_np_test(NAME restart-scenarios-if-test-none COMMAND tests/restart-scenarios-test.py -c none --kill-sig term -p4 -v --activate-if ${UNSHARE})
add_np_test(NAME terminate-scenarios-test-resync COMMAND tests/terminate-scenarios-test.py -c resync --terminate-at-block 10 --kill-sig term ${UNSHARE})
add_np_test(NAME terminate-scenarios-test-replay COMMAND tests/terminate-scenarios-test.py -c replay --terminate-at-block 10 --kill-sig term ${UNSHARE})
add_np_test(NAME terminate-scenarios-test-hard_replay COMMAND tests/terminate-scenarios-test.py -c hardReplay --terminate-at-block 10 --kill-sig term ${UNSHARE})
add_np_test(NAME terminate-scenarios-if-test-resync COMMAND tests/terminate-scenarios-test.py -c resync --terminate-at-block 10 --kill-sig term --activate-if ${UNSHARE})
add_np_test(NAME terminate-scenarios-if-test-replay COMMAND tests/terminate-scenarios-test.py -c replay --terminate-at-block 10 --kill-sig term --activate-if ${UNSHARE})
add_np_test(NAME terminate-scenarios-if-test-hard_replay COMMAND tests/terminate-scenarios-test.py -c hardReplay --terminate-at-block 10 --kill-sig term --activate-if ${UNSHARE})
add_np_test(NAME terminate-scenarios-if-test-replay-pass-transition COMMAND tests/terminate-scenarios-test.py -c replay --terminate-at-block 150 --kill-sig term --activate-if ${UNSHARE})
add_np_test(NAME terminate-scenarios-if-test-hard_replay-pass-transition COMMAND tests/terminate-scenarios-test.py -c hardReplay --terminate-at-block 150 --kill-sig term --activate-if ${UNSHARE})
add_np_test(NAME validate_dirty_db_test COMMAND tests/validate-dirty-db.py -v ${UNSHARE})
add_np_test(NAME keosd_auto_launch_test COMMAND tests/keosd_auto_launch_test.py)
add_np_test(NAME nodeos_snapshot_diff_test COMMAND tests/nodeos_snapshot_diff_test.py -v ${UNSHARE})
add_np_test(NAME nodeos_snapshot_diff_if_test COMMAND tests/nodeos_snapshot_diff_test.py -v --activate-if ${UNSHARE})
add_np_test(NAME nodeos_snapshot_forked_test COMMAND tests/nodeos_snapshot_forked_test.py -v ${UNSHARE})
add_np_test(NAME nodeos_snapshot_forked_if_test COMMAND tests/nodeos_snapshot_forked_test.py -v --activate-if ${UNSHARE})
add_np_test(NAME nodeos_late_block_test COMMAND tests/nodeos_late_block_test.py -v ${UNSHARE})

add_np_test(NAME trx_finality_status_test COMMAND tests/trx_finality_status_test.py -v ${UNSHARE})
add_np_test(NAME trx_finality_status_if_test COMMAND tests/trx_finality_status_test.py -v --activate-if ${UNSHARE})

add_np_test(NAME trx_finality_status_forked_test COMMAND tests/trx_finality_status_forked_test.py -v ${UNSHARE})
add_np_test(NAME trx_finality_status_forked_if_test COMMAND tests/trx_finality_status_forked_test.py -v --activate-if ${UNSHARE})

add_p_test(NAME db_modes_test COMMAND tests/db_modes_test.sh -v COST 6000)
add_p_test(NAME version-label-test COMMAND tests/version-label.sh "v${VERSION_FULL}")
add_p_test(NAME full-version-label-test COMMAND tests/full-version-label.sh "v${VERSION_FULL}" ${CMAKE_SOURCE_DIR})
add_np_test(NAME nested_container_multi_index_test COMMAND tests/nested_container_multi_index_test.py -n 2)

add_np_test(NAME p2p_multiple_listen_test COMMAND tests/p2p_multiple_listen_test.py -v ${UNSHARE})
add_np_test(NAME p2p_multiple_listen_if_test COMMAND tests/p2p_multiple_listen_test.py -v --activate-if ${UNSHARE})
add_np_test(NAME p2p_no_listen_test COMMAND tests/p2p_no_listen_test.py -v ${UNSHARE})
add_lr_test(NAME p2p_sync_throttle_test COMMAND tests/p2p_sync_throttle_test.py -v -d 2 ${UNSHARE})
add_lr_test(NAME p2p_sync_throttle_if_test COMMAND tests/p2p_sync_throttle_test.py -v -d 2 --activate-if ${UNSHARE})
add_np_test(NAME p2p_no_blocks_if_test COMMAND tests/p2p_no_blocks_test.py -v -d 2 ${UNSHARE})

# needs iproute-tc or iproute2 depending on platform
#add_np_test(NAME p2p_high_latency_test COMMAND tests/p2p_high_latency_test.py -v)

# This test is too much for CI/CD machines. We do run it with fewer nodes as a nonparallelizable_tests above
#add_lr_test(NAME distributed_transactions_lr_test COMMAND tests/distributed-transactions-test.py -d 2 -p 21 -n 21 -v)
#add_lr_test(NAME distributed_transactions_if_lr_test COMMAND tests/distributed-transactions-test.py -d 2 -p 21 -n 21 --activate-if -v)

add_lr_test(NAME nodeos_forked_chain_lr_test COMMAND tests/nodeos_forked_chain_test.py -v --wallet-port 9901 ${UNSHARE})
add_lr_test(NAME nodeos_forked_chain_if_lr_test COMMAND tests/nodeos_forked_chain_test.py -v --activate-if --wallet-port 9901 ${UNSHARE})

add_np_test(NAME nodeos_contrl_c_test COMMAND tests/nodeos_contrl_c_test.py -v --wallet-port 9901 ${UNSHARE})
add_np_test(NAME nodeos_contrl_c_if_test COMMAND tests/nodeos_contrl_c_test.py --activate-if -v --wallet-port 9901 ${UNSHARE})

add_lr_test(NAME nodeos_voting_lr_test COMMAND tests/nodeos_voting_test.py -v --wallet-port 9902 ${UNSHARE})
add_lr_test(NAME nodeos_voting_if_lr_test COMMAND tests/nodeos_voting_test.py -v --activate-if --wallet-port 9902 ${UNSHARE})

add_lr_test(NAME nodeos_under_min_avail_ram_lr_test COMMAND tests/nodeos_under_min_avail_ram.py -v --wallet-port 9904 ${UNSHARE})
add_lr_test(NAME nodeos_under_min_avail_ram_if_lr_test COMMAND tests/nodeos_under_min_avail_ram.py -v --activate-if ${UNSHARE})

add_lr_test(NAME nodeos_irreversible_mode_lr_test COMMAND tests/nodeos_irreversible_mode_test.py -v ${UNSHARE})
add_lr_test(NAME nodeos_irreversible_mode_if_lr_test COMMAND tests/nodeos_irreversible_mode_test.py -v --activate-if ${UNSHARE})

add_lr_test(NAME nodeos_read_terminate_at_block_lr_test COMMAND tests/nodeos_read_terminate_at_block_test.py -v ${UNSHARE})
add_lr_test(NAME nodeos_read_terminate_at_block_if_lr_test COMMAND tests/nodeos_read_terminate_at_block_test.py --activate-if -v ${UNSHARE})

add_np_test(NAME liveness_test COMMAND tests/liveness_test.py -v ${UNSHARE})

add_np_test(NAME nodeos_chainbase_allocation_test COMMAND tests/nodeos_chainbase_allocation_test.py -v ${UNSHARE})
add_np_test(NAME nodeos_chainbase_allocation_if_test COMMAND tests/nodeos_chainbase_allocation_test.py --activate-if -v ${UNSHARE})

add_np_test(NAME nodeos_signal_throw_test COMMAND tests/nodeos_signal_throw_test.py -v ${UNSHARE})

add_lr_test(NAME nodeos_startup_catchup_lr_test COMMAND tests/nodeos_startup_catchup.py -v ${UNSHARE})
add_lr_test(NAME nodeos_startup_catchup_if_lr_test COMMAND tests/nodeos_startup_catchup.py -p3 --activate-if -v ${UNSHARE})

add_np_test(NAME nodeos_short_fork_take_over_test COMMAND tests/nodeos_short_fork_take_over_test.py -v --wallet-port 9905 ${UNSHARE})
add_np_test(NAME nodeos_short_fork_take_over_if_test COMMAND tests/nodeos_short_fork_take_over_test.py -v --activate-if --wallet-port 9905 ${UNSHARE})

add_np_test(NAME nodeos_extra_packed_data_test COMMAND tests/nodeos_extra_packed_data_test.py -v -p 2 ${UNSHARE})

add_lr_test(NAME nodeos_producer_watermark_lr_test COMMAND tests/nodeos_producer_watermark_test.py -v ${UNSHARE})
add_lr_test(NAME nodeos_producer_watermark_if_lr_test COMMAND tests/nodeos_producer_watermark_test.py -v --activate-if ${UNSHARE})

add_lr_test(NAME nodeos_high_transaction_lr_test COMMAND tests/nodeos_high_transaction_test.py -p 4 -n 8 --num-transactions 10000 --max-transactions-per-second 500 ${UNSHARE})
add_lr_test(NAME nodeos_high_transaction_if_lr_test COMMAND tests/nodeos_high_transaction_test.py --activate-if -p 4 -n 8 --num-transactions 10000 --max-transactions-per-second 500 ${UNSHARE})

add_lr_test(NAME nodeos_retry_transaction_lr_test COMMAND tests/nodeos_retry_transaction_test.py -v --num-transactions 100 --max-transactions-per-second 10 --total-accounts 5 ${UNSHARE})
add_lr_test(NAME nodeos_retry_transaction_if_lr_test COMMAND tests/nodeos_retry_transaction_test.py -v --activate-if --num-transactions 100 --max-transactions-per-second 10 --total-accounts 5 ${UNSHARE})

add_np_test(NAME cli_test COMMAND tests/cli_test.py)

add_np_test(NAME lib_advance_test COMMAND tests/lib_advance_test.py -v ${UNSHARE})
add_np_test(NAME lib_advance_if_test COMMAND tests/lib_advance_test.py --activate-if -v ${UNSHARE})

add_p_test(NAME spring_util_bls_test COMMAND tests/spring_util_bls_test.py)
add_p_test(NAME spring_util_snapshot_info_test COMMAND tests/spring_util_snapshot_info_test.py)

add_np_test(NAME http_plugin_test COMMAND tests/http_plugin_test.py ${UNSHARE} TIMEOUT 100)

add_np_test(NAME plugin_http_api_test COMMAND tests/plugin_http_api_test.py TIMEOUT 50)

add_np_test(NAME plugin_http_api_test_savanna COMMAND tests/plugin_http_api_test_savanna.py TIMEOUT 240)

add_np_test(NAME plugin_http_category_api_test COMMAND tests/plugin_http_api_test.py TIMEOUT 50)
set_tests_properties(plugin_http_category_api_test PROPERTIES ENVIRONMENT "PLUGIN_HTTP_TEST_CATEGORY=ON")

add_np_test(NAME trace_plugin_test COMMAND tests/trace_plugin_test.py -v TIMEOUT 300)

add_lr_test(NAME resource_monitor_plugin_test COMMAND tests/resource_monitor_plugin_test.py)

add_lr_test(NAME nodeos_repeat_transaction_lr_test COMMAND tests/nodeos_high_transaction_test.py -v -p 4 -n 8 --num-transactions 1000 --max-transactions-per-second 500 --send-duplicates ${UNSHARE})

add_np_test(NAME light_validation_sync_test COMMAND tests/light_validation_sync_test.py -v ${UNSHARE})
add_np_test(NAME light_validation_sync_if_test COMMAND tests/light_validation_sync_test.py --activate-if -v ${UNSHARE})
add_np_test(NAME interrupt_trx_test COMMAND tests/interrupt_trx_test.py -v ${UNSHARE})

add_lr_test(NAME auto_bp_peering_test COMMAND tests/auto_bp_peering_test.py -v ${UNSHARE})
add_lr_test(NAME auto_bp_peering_if_test COMMAND tests/auto_bp_peering_test.py -v --activate-if ${UNSHARE})
add_lr_test(NAME auto_bp_gossip_peering_test COMMAND tests/auto_bp_gossip_peering_test.py -v ${UNSHARE})

add_np_test(NAME gelf_test COMMAND tests/gelf_test.py ${UNSHARE})

add_lr_test(NAME performance_test_bp COMMAND tests/PerformanceHarnessScenarioRunner.py findMax testBpOpMode --max-tps-to-test 50 --test-iteration-min-step 10 --test-iteration-duration-sec 10 --final-iterations-duration-sec 10 --calc-chain-threads lmax overrideBasicTestConfig -v --tps-limit-per-generator 25 --chain-state-db-size-mb 200 ${UNSHARE})
add_lr_test(NAME performance_test_api COMMAND tests/PerformanceHarnessScenarioRunner.py findMax testApiOpMode --max-tps-to-test 50 --test-iteration-min-step 10 --test-iteration-duration-sec 10 --final-iterations-duration-sec 10 --calc-chain-threads lmax overrideBasicTestConfig -v --tps-limit-per-generator 25 --chain-state-db-size-mb 200 ${UNSHARE})
add_lr_test(NAME performance_test_read_only_trxs COMMAND tests/PerformanceHarnessScenarioRunner.py findMax testApiOpMode --max-tps-to-test 50 --test-iteration-min-step 10 --test-iteration-duration-sec 10 --final-iterations-duration-sec 10 overrideBasicTestConfig -v --tps-limit-per-generator 25 --api-nodes-read-only-threads 2 --read-only-write-window-time-us 1000 --read-only-read-window-time-us 375000 --account-name "payloadless" --abi-file payloadless.abi --wasm-file payloadless.wasm --contract-dir unittests/test-contracts/payloadless --user-trx-data-file tests/PerformanceHarness/readOnlySlowTrxData.json --chain-state-db-size-mb 200 ${UNSHARE})
add_lr_test(NAME performance_test_cpu_trx_spec COMMAND tests/PerformanceHarnessScenarioRunner.py findMax testBpOpMode --max-tps-to-test 50 --test-iteration-min-step 10 --test-iteration-duration-sec 10 --final-iterations-duration-sec 10 overrideBasicTestConfig -v --tps-limit-per-generator 25 --chain-state-db-size-mb 200 --account-name "c" --abi-file eosmechanics.abi --wasm-file eosmechanics.wasm --contract-dir unittests/contracts/eosio.mechanics --user-trx-data-file tests/PerformanceHarness/cpuTrxData.json ${UNSHARE})
add_np_test(NAME performance_test_basic_p2p COMMAND tests/PerformanceHarnessScenarioRunner.py singleTest -v --producer-nodes 1 --validation-nodes 1 --target-tps 20 --tps-limit-per-generator 10 --test-duration-sec 5 --chain-state-db-size-mb 200 ${UNSHARE})
add_np_test(NAME performance_test_basic_http COMMAND tests/PerformanceHarnessScenarioRunner.py singleTest -v --endpoint-mode http --producer-nodes 1 --validation-nodes 1 --api-nodes 1 --target-tps 10 --tps-limit-per-generator 10 --test-duration-sec 5 --chain-state-db-size-mb 200 ${UNSHARE})
add_np_test(NAME performance_test_basic_transfer_trx_spec COMMAND tests/PerformanceHarnessScenarioRunner.py singleTest -v --producer-nodes 1 --validation-nodes 1 --target-tps 20 --tps-limit-per-generator 10 --test-duration-sec 5 --chain-state-db-size-mb 200 --user-trx-data-file tests/PerformanceHarness/userTrxDataTransfer.json ${UNSHARE})
add_np_test(NAME performance_test_basic_new_acct_trx_spec COMMAND tests/PerformanceHarnessScenarioRunner.py singleTest -v --producer-nodes 1 --validation-nodes 1 --target-tps 20 --tps-limit-per-generator 10 --test-duration-sec 5 --chain-state-db-size-mb 200 --user-trx-data-file tests/PerformanceHarness/userTrxDataNewAccount.json ${UNSHARE})
add_np_test(NAME performance_test_basic_cpu_trx_spec COMMAND tests/PerformanceHarnessScenarioRunner.py singleTest -v --producer-nodes 1 --validation-nodes 1 --target-tps 20 --tps-limit-per-generator 10 --test-duration-sec 5 --chain-state-db-size-mb 200 --account-name "c" --abi-file eosmechanics.abi --wasm-file eosmechanics.wasm --contract-dir unittests/contracts/eosio.mechanics --user-trx-data-file tests/PerformanceHarness/cpuTrxData.json ${UNSHARE})
add_np_test(NAME performance_test_basic_ram_trx_spec COMMAND tests/PerformanceHarnessScenarioRunner.py singleTest -v --producer-nodes 1 --validation-nodes 1 --target-tps 20 --tps-limit-per-generator 10 --test-duration-sec 5 --chain-state-db-size-mb 200 --account-name "r" --abi-file eosmechanics.abi --wasm-file eosmechanics.wasm --contract-dir unittests/contracts/eosio.mechanics --user-trx-data-file tests/PerformanceHarness/ramTrxData.json ${UNSHARE})
add_np_test(NAME performance_test_basic_net_trx_spec COMMAND tests/PerformanceHarnessScenarioRunner.py singleTest -v --producer-nodes 1 --validation-nodes 1 --target-tps 20 --tps-limit-per-generator 10 --test-duration-sec 5 --chain-state-db-size-mb 200 --account-name "n" --abi-file eosmechanics.abi --wasm-file eosmechanics.wasm --contract-dir unittests/contracts/eosio.mechanics --user-trx-data-file tests/PerformanceHarness/netTrxData.json ${UNSHARE})
add_np_test(NAME performance_test_basic_read_only_trxs COMMAND tests/PerformanceHarnessScenarioRunner.py singleTest -v --endpoint-mode http --producer-nodes 1 --validation-nodes 1 --api-nodes 1 --api-nodes-read-only-threads 2 --read-only-write-window-time-us 1000 --read-only-read-window-time-us 375000 --target-tps 10 --tps-limit-per-generator 5 --test-duration-sec 5 --chain-state-db-size-mb 200 --account-name "payloadless" --abi-file payloadless.abi --wasm-file payloadless.wasm --contract-dir unittests/test-contracts/payloadless --user-trx-data-file tests/PerformanceHarness/readOnlySlowTrxData.json ${UNSHARE})

if(ENABLE_COVERAGE_TESTING)

  set(Coverage_NAME ${PROJECT_NAME}_coverage)

  if(NOT LCOV_PATH)
    message(FATAL_ERROR "lcov not found! Aborting...")
  endif() # NOT LCOV_PATH

  if(NOT LLVMCOV_PATH)
    message(FATAL_ERROR "llvm-cov not found! Aborting...")
  endif()

  if(NOT GENHTML_PATH)
    message(FATAL_ERROR "genhtml not found! Aborting...")
  endif() # NOT GENHTML_PATH

  # no spaces allowed within tests list
  set(ctest_tests 'plugin_test|p2p_dawn515_test|nodeos_run_test|distributed-transactions-test|restart-scenarios-test_resync')
  set(ctest_exclude_tests 'distributed-transactions-remote-test|restart-scenarios-test_replay')

  # Setup target
  add_custom_target(${Coverage_NAME}

    COMMAND ${LCOV_PATH} --directory . --zerocounters

    COMMAND ctest -R ${ctest_tests} -E ${ctest_exclude_tests}

    COMMENT "Capturing lcov counters and generating report"
    COMMAND ${LCOV_PATH} --directory . --capture --gcov-tool ${CMAKE_SOURCE_DIR}/tools/llvm-gcov.sh --output-file ${Coverage_NAME}.info

    COMMENT "Processing code coverage counters and generating report."
    COMMAND ${GENHTML_PATH} -o ${Coverage_NAME} ${PROJECT_BINARY_DIR}/${Coverage_NAME}.info
    COMMAND ${CMAKE_COMMAND} -E remove ${Coverage_NAME}.base ${Coverage_NAME}.info ${Coverage_NAME}.total ${PROJECT_BINARY_DIR}/${Coverage_NAME}.info.cleaned

    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    COMMENT "Resetting code coverage counters to zero. Processing code coverage counters and generating report."
    )

  # Show info where to find the report
  add_custom_command(TARGET ${Coverage_NAME} POST_BUILD
    COMMAND ;
    COMMENT "Open ./${Coverage_NAME}/index.html in your browser to view the coverage report."
    )
endif()
