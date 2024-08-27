#!/usr/bin/env python3
import os
import shutil
import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

###############################################################
# production_restart
#
# Tests restart of a production node with a pending finalizer policy.
#
# Start up a network with two nodes. The first node has a producer (defproducera) and
# a single finalizer key configured. The second node has a producer (defproducerb) and
# a single finalizer key configured. Use the bios contract to transition
# to Savanna consensus while keeping the existing producers and using a finalizer
# policy with the two finalizers.
#
# Once everything has been confirmed to be working correctly and finality is advancing,
# cleanly shut down the defproducera node but keep the defproducerb node running.
#
# Then change the finalizer policy using an unconfigured key in the defproducera node
# to guarantee to get the node stay in a state where it has a pending finalizer policy
# because the key was not configured. At that point restart the defproducera node with
# new key configured and stale production enabled so it produces blocks again.
#
# The correct behavior is for votes from the defproducerb node on the newly produced
# blocks to be accepted by the defproducerb, QCs to be formed and included in new blocks,
# and finality to advance.
#
# Due to the bug in pre-1.0.0-rc1, we expect that on restart the defproducera node
# will reject the votes received by the defproducerb node because the defproducera
# node will be computing the wrong finality digest.
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-d","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"})
pnodes=2
delay=args.d
debug=args.v
prod_count = 1 # per node prod count
total_nodes=pnodes
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, keepRunning=args.leave_running, keepLogs=args.keep_logs)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    Print("Stand up cluster")
    # For now do not load system contract as it does not support setfinalizer
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, totalProducers=pnodes, delay=delay, loadSystemContract=False,
                      activateIF=True, topo="./tests/production_restart_test_shape.json") is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    producerNode = cluster.getNode(0)
    finalizerNode = cluster.getNode(1)

    Print("Wait for lib to advance")
    assert finalizerNode.waitForLibToAdvance(), "finalizerNode did not advance LIB"
    assert producerNode.waitForLibToAdvance(), "producerNode did not advance LIB"

    Print("Set finalizers so a pending is in play")
    # Use an unconfigured key for new finalizer policy on producerNode such that
    # producerNode stays in a state where it has a pending finalizer policy.
    producerNode.keys[0].blspubkey = "PUB_BLS_JzblSr2sf_UhxQjGxOtHbRCBkHgSB1RG4xUbKKl-fKtUjx6hyOHajnVQT4IvBF4PutlX7JTC14IqIjADlP-3_G2MXRhBlkB57r2u59OCwRQQEDqmVSADf6CoT8zFUXcSgHFw7w" # setFinalizers uses the first key in key list (index 0)
    producerNode.keys[0].blspop    = "SIG_BLS_Z5fJqFv6DIsHFhBFpkHmL_R48h80zVKQHtB5lrKGOVZTaSQNuVaXD_eHg7HBvKwY6zqgA_vryCLQo5W0Inu6HtLkGL2gYX2UHJjrZJZpfJSKG0ynqAZmyrCglxRLNm8KkFdGGR8oJXf5Yzyu7oautqTPniuKLBvNeQxGJGDOQtHSQ0uP3mD41pWzPFRoi10BUor9MbwUTQ7fO7Of4ZjhVM3IK4JrqX1RBXkDX83Wi9xFzs_fdPIyMqmgEzFgolgUa8XN4Q"

    assert cluster.setFinalizers([producerNode, finalizerNode], producerNode), "setfinalizers failed"
    assert producerNode.waitForLibToAdvance(), "producerNode did not advance LIB after setfinalizers"
    # Wait for head to advance twice to make sure pending policy is in place
    producerNode.waitForHeadToAdvance()
    producerNode.waitForHeadToAdvance()

    # Check if a pending policy exists
    finalizerInfo = producerNode.getFinalizerInfo()
    Print(f"{finalizerInfo}")
    if (finalizerInfo["payload"]["pending_finalizer_policy"] is not None
        and finalizerInfo["payload"]["pending_finalizer_policy"]["finalizers"] is not None):
        Print("pending policy exists")
    else:
        Utils.errorExit("pending policy does not exist")

    Print("Shutdown producer producerNode")
    producerNode.kill(signal.SIGTERM)
    assert not producerNode.verifyAlive(), "producerNode did not shutdown"

    # Configure the new key (using --signature-provider) and restart producerNode.
    # LIB should advance
    Print("Restart producer producerNode")
    producerNode.relaunch(chainArg=" -e --signature-provider PUB_BLS_JzblSr2sf_UhxQjGxOtHbRCBkHgSB1RG4xUbKKl-fKtUjx6hyOHajnVQT4IvBF4PutlX7JTC14IqIjADlP-3_G2MXRhBlkB57r2u59OCwRQQEDqmVSADf6CoT8zFUXcSgHFw7w=KEY:PVT_BLS_QRxLAVbe2n7RaPWx2wHbur8erqUlAs-V_wXasGhjEA78KlBq")

    Print("Verify LIB advances after restart")
    assert producerNode.waitForLibToAdvance(), "producerNode did not advance LIB"
    assert finalizerNode.waitForLibToAdvance(), "finalizerNode did not advance LIB"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
