#!/usr/bin/env python3
import os
import shutil
import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

###############################################################
# disaster_recovery - Scenario 1
#
# Integration test with 4 finalizers (A, B, C, and D).
#
#   The 4 nodes are cleanly shutdown in the following state:
#   - A has LIB N. A has a finalizer safety information file that locks on a block after N.
#   - B, C, and D have LIB less than N. They have finalizer safety information files that lock on N.
#
#   Nodes B, C, and D lose their reversible blocks. All nodes restart from an earlier snapshot.
#
#   A is restarted and replays up to its last reversible block (which is a block number greater than N) after
#   restarting from snapshot. Blocks N and later is sent to the other nodes B, C, and D after they are also
#   started up again.
#
#   Verify that LIB advances and that A, B, C, and D are eventually voting strong on new blocks.
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-d","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"})
pnodes=4
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
                      activateIF=True) is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    node0 = cluster.getNode(0) # A
    node1 = cluster.getNode(1) # B
    node2 = cluster.getNode(2) # C
    node3 = cluster.getNode(3) # D

    Print("Create snapshot (node 0)")
    ret = node0.createSnapshot()
    assert ret is not None, "Snapshot creation failed"
    ret_head_block_num = ret["payload"]["head_block_num"]
    Print(f"Snapshot head block number {ret_head_block_num}")

    Print("Wait for snapshot node lib to advance")
    node0.waitForBlock(ret_head_block_num+1, blockType=BlockType.lib)
    assert node1.waitForLibToAdvance(), "Ndoe1 did not advance LIB after snapshot of Node0"

    assert node0.waitForLibToAdvance(), "Node0 did not advance LIB after snapshot"
    currentLIB = node0.getIrreversibleBlockNum()

    for node in [node1, node2, node3]:
        node.kill(signal.SIGTERM)

    for node in [node1, node2, node3]:
        assert not node.verifyAlive(), "Node did not shutdown"

    # node0 will have higher lib than 1,2,3 since it can incorporate QCs in blocks
    Print("Wait for node 0 LIB to advance")
    assert node0.waitForBlock(currentLIB, blockType=BlockType.lib), "Node0 did not advance LIB" # uses getBlockNum(blockType=blockType) > blockNum
    node0.kill(signal.SIGTERM)
    assert not node0.verifyAlive(), "Node0 did not shutdown"

    node0.removeState()
    for node in [node1, node2, node3]:
        node.removeReversibleBlks()
        node.removeState()

    for i in range(4):
        isRelaunchSuccess = cluster.getNode(i).relaunch(chainArg=" -e --snapshot {}".format(node0.getLatestSnapshot()))
        assert isRelaunchSuccess, f"node {i} relaunch from snapshot failed"

    for node in [node0, node1, node2, node3]:
        assert node.waitForLibToAdvance(), "Node did not advance LIB after relaunch"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
