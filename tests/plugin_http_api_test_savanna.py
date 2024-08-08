#!/usr/bin/env python3

import json
import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr

###############################################################
# plugin_http_api_test_savanna.py
# 
# Tests of Savanna specific HTTP RPC endpoints are placed in this file.
#
# Note: plugin_http_api_test.py does not use test Cluster and need to
#       test protocol feature activation, it is not suitable for tests
#       involving Savanna.
###############################################################

Print=Utils.Print

args = TestHelper.parse_args({"--dump-error-details","--keep-logs","-v","--leave-running","--unshared"})

Utils.Debug=args.v
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
dumpErrorDetails=args.dump_error_details
walletPort=TestHelper.DEFAULT_WALLET_PORT

# Setup 4 nodes such that to verify multiple finalizers in get_finalizer_info test
totalProducerNodes=3
totalNonProducerNodes=1
totalNodes=totalProducerNodes+totalNonProducerNodes

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)
    Print("Stand up cluster")

    if cluster.launch(topo="mesh", pnodes=totalProducerNodes, totalNodes=totalNodes,
                      activateIF=True) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up cluster.")

    # Verify nodes are in sync and advancing
    cluster.waitOnClusterSync(blockAdvancing=5)
    Print("Cluster in Sync")

    Print("Shutdown unneeded bios node")
    cluster.biosNode.kill(signal.SIGTERM)

    node = cluster.nodes[0]
    resource = "chain"
    payload = {}
    empty_content_dict = {}
    http_post_invalid_param = '{invalid}'

    # get_finalizer_info with empty parameter
    command = "get_finalizer_info"
    ret_json = node.processUrllibRequest(resource, command, payload)
    assert type(ret_json["payload"]["active_finalizer_policy"]) == dict
    assert type(ret_json["payload"]["last_tracked_votes"]) == list
    node.waitForLibToAdvance()
    org_ret_json = ret_json
    # get_finalizer_info with empty content parameter
    ret_json = node.processUrllibRequest(resource, command, empty_content_dict, payload)
    assert org_ret_json != ret_json, f"{ret_json}"
    assert type(ret_json["payload"]["active_finalizer_policy"]) == dict
    assert type(ret_json["payload"]["last_tracked_votes"]) == list
    # get_finalizer_info with invalid parameter
    ret_json = node.processUrllibRequest(resource, command, http_post_invalid_param, payload)
    assert ret_json["code"] == 400
    assert ret_json["error"]["code"] == 3200006

    testSuccessful = True

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
