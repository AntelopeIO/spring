#!/usr/bin/env python3
import os
import shutil
import signal
import time
from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

###############################################################
# disaster_recovery - Scenario 3
#
# Integration test with 4 nodes (A, B, C, and D), each having its own producer and finalizer. The finalizer policy
# consists of the four finalizers with a threshold of 3. The proposer policy involves all four proposers.
#
# - At least two of the four nodes have a LIB N and a finalizer safety information file that locks on a block
#   after N. The other two nodes have a LIB that is less than or equal to block N.
#
# All nodes are shut down. The reversible blocks on all nodes is deleted. Restart all nodes from an earlier snapshot.
#
# All nodes eventually sync up to block N. Some nodes will consider block N to LIB but others may not.
#
# Not enough finalizers should be voting because of the lock in their finalizer safety information file. Verify that
# LIB does not advance on any node.
#
# Cleanly shut down all nodes and delete their finalizer safety information files. Then restart the nodes.
#
# Verify that LIB advances on all nodes and they all agree on the LIB. In particular, verify that block N is the
# same ID on all nodes as the one before nodes were first shutdown.
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
    extraNodeosArgs = " --plugin eosio::producer_api_plugin "
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, totalProducers=pnodes, delay=delay, loadSystemContract=False,
                      extraNodeosArgs=extraNodeosArgs, activateIF=True, biosFinalizer=False) is False:
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
    assert node0.waitForBlock(ret_head_block_num+1, blockType=BlockType.lib), "Node0 did not advance to make snapshot block LIB"
    assert node1.waitForLibToAdvance(), "Node1 did not advance LIB after snapshot of Node0"
    assert node0.waitForLibToAdvance(), "Node0 did not advance LIB after snapshot"

    Print("Stop production on all nodes")
    assert node0.waitForProducer("defproducera"), "Node 0 did not produce"
    for node in [node0, node1, node2, node3]:
        node.processUrllibRequest("producer", "pause", exitOnError=True)
    assert node0.waitForLibNotToAdvance(), "Node0 LIB is still advancing"

    currentLIB = node0.getIrreversibleBlockNum()
    currentLIB1 = node1.getIrreversibleBlockNum()
    assert currentLIB == currentLIB1, f"Node0 {currentLIB} and Node1 {currentLIB1} LIBs do not match"
    n_LIB = currentLIB + 1
    libBlock = node0.getBlock(n_LIB)

    Print("Shutdown two nodes at LIB N-1, should be locked on block after N")
    for node in [node0, node1]:
        node.kill(signal.SIGTERM)
    for node in [node0, node1]:
        assert not node.verifyAlive(), "Node did not shutdown"

    Print("Resume production on Node2 and Node3, after shutdown of Node0 and Node1, LIB can no longer advance")
    for node in [node2, node3]:
        node.processUrllibRequest("producer", "resume", exitOnError=True)

    Print("Wait for lib to advance to LIB N on other 2 nodes, LIB should not advance any further since Node0 and Node1 shutdown")
    for node in [node2, node3]:
        assert node.waitForBlock(n_LIB, timeout=None, blockType=BlockType.lib), "Node did not advance LIB after shutdown of node0 and node1"
        currentLIB = node.getIrreversibleBlockNum()
        assert currentLIB == n_LIB, f"Node {node.nodeId} advanced LIB {currentLIB} beyond N LIB {n_LIB}"

    Print("Shutdown other two nodes")
    for node in [node2, node3]:
        node.kill(signal.SIGTERM)
    for node in [node2, node3]:
        assert not node.verifyAlive(), "Node did not shutdown"

    Print("Remove reversible blocks and state, but not finalizers safety data")
    for node in [node0, node1, node2, node3]:
        node.removeReversibleBlks()
        node.removeState()

    Print("Restart nodes from snapshot")
    for i in range(4):
        isRelaunchSuccess = cluster.getNode(i).relaunch(chainArg=" -e --snapshot {}".format(node0.getLatestSnapshot()))
        assert isRelaunchSuccess, f"node {i} relaunch from snapshot failed"

    Print("Verify forks resolve and libBlock is included on all nodes")
    Print(f"Lib Block: {libBlock}")
    for node in [node0, node1, node2, node3]:
        node.waitForBlock(n_LIB)
        nodeId = node.getBlock(n_LIB)["id"]
        assert nodeId == libBlock["id"], "Node lib block id does not match prior lib block id"

    Print("Verify LIB does not advance on any node")
    for node in [node0, node1, node2, node3]:
        assert not node.waitForLibToAdvance(), "Node advanced LIB after relaunch when it should not"

    Print("Shutdown all nodes to remove finalizer safety data")
    for node in [node0, node1, node2, node3]:
        node.kill(signal.SIGTERM)
    for node in [node0, node1, node2, node3]:
        assert not node.verifyAlive(), "Node did not shutdown"

    Print("Remove finalizer safety data")
    for node in [node0, node1, node2, node3]:
        node.removeFinalizersSafetyDir()

    Print("Restart nodes")
    for node in [node0, node1, node2, node3]:
        node.relaunch(rmArgs=" --snapshot {}".format(node0.getLatestSnapshot()))

    Print("Verify LIB advances on all nodes")
    for node in [node0, node1, node2, node3]:
        assert node.waitForLibToAdvance(), "Node did not advance LIB after restart"

    for node in [node0, node1, node2, node3]:
        nodeId = node.getBlock(n_LIB)["id"]
        assert nodeId == libBlock["id"], "Node lib block id does not match prior lib block id"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
