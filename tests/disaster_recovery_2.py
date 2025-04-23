#!/usr/bin/env python3
import os
import shutil
import signal
import time
from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

###############################################################
# disaster_recovery - Scenario 2
#
# Verify that if finalizers are only locked on LIB blocks then all reversable blocks in the network can be lost
# and consensus can continue.
#
# Integration test with 5 nodes (A, B, C, D, and P). Nodes A, B, C, and D each have one finalizer but no proposers.
# Node P has a proposer but no finalizers. The finalizer policy consists of the four finalizers with a threshold of 3.
# The proposer policy involves just the single proposer P.
#
# A, B, C, and D can be connected to each other however we like as long as blocks sent to node A can traverse to the
# other nodes B, C, and D. However, node P should only be connected to node A.
#
# At some point after IF transition has completed and LIB is advancing, block production on node P should be paused.
# Enough time should be given to allow and in-flight votes on the latest produced blocks to be delivered to node P.
# Then, the connection between node P and node A should be severed, and then block production on node P resumed. The
# LIB on node P should advance to but then stall at block N. Then shortly after that, node P should be cleanly shut down.
#
# Verify that the LIB on A, B, C, and D has stalled and is less than block N. Then, nodes A, B, C, and D can all be
# cleanly shut down.
#
# Then, reversible blocks from all nodes should be removed. All nodes are restarted from an earlier
# snapshot (prior to block N).
#
# P is restarted and replays up to block N after restarting from snapshot. Blocks up to and including block N are sent
# to the other nodes A, B, C, and D after they are also started up again.
#
# Verify that LIB advances and that A, B, C, and D are eventually voting strong on new blocks.
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-d","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"})
pnodes=1
delay=args.d
debug=args.v
prod_count = 1 # per node prod count
total_nodes=pnodes+4
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
    specificExtraNodeosArgs={}
    specificExtraNodeosArgs[0]="--plugin eosio::net_api_plugin --plugin eosio::producer_api_plugin "

    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, totalProducers=pnodes, specificExtraNodeosArgs=specificExtraNodeosArgs,
                      topo="./tests/disaster_recovery_2_test_shape.json", delay=delay, loadSystemContract=False,
                      activateIF=True, signatureProviderForNonProducer=True) is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"

    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    node0 = cluster.getNode(0) # P
    node1 = cluster.getNode(1) # A
    node2 = cluster.getNode(2) # B
    node3 = cluster.getNode(3) # C
    node4 = cluster.getNode(4) # D

    Print("Create snapshot (node 0)")
    ret = node0.createSnapshot()
    assert ret is not None, "Snapshot creation failed"
    ret_head_block_num = ret["payload"]["head_block_num"]
    Print(f"Snapshot head block number {ret_head_block_num}")

    Print("Wait for snapshot node lib to advance")
    assert node0.waitForBlock(ret_head_block_num+1, blockType=BlockType.lib), "Node0 did not advance to make snapshot block LIB"
    assert node1.waitForLibToAdvance(), "Node1 did not advance LIB after snapshot of Node0"

    assert node0.waitForLibToAdvance(), "Node0 did not advance LIB after snapshot"

    Print("Pause production on Node0")
    node0.processUrllibRequest("producer", "pause")
    node0.waitForHeadToAdvance()
    lib = node0.getIrreversibleBlockNum()
    node0.waitForHeadToAdvance(timeout=3) # should time out

    Print("Disconnect the producing node (Node0) from peer Node1")
    node0.processUrllibRequest("net", "disconnect", "localhost:9877")
    assert not node0.waitForLibToAdvance(timeout=10), "Node0 LIB still advancing after disconnect"
    assert not node1.waitForHeadToAdvance(timeout=5), "Node1 head still advancing after disconnect"

    Print("Resume production on Node0")
    node0.processUrllibRequest("producer", "resume")
    assert node0.waitForIrreversibleBlock(lib+1)
    libN = node0.getIrreversibleBlockNum()
    assert libN > lib

    for node in [node1, node2, node3, node4]:
        lib = node.getIrreversibleBlockNum()
        assert lib < libN, f"Node LIB {lib} >= LIB N {libN}"

    Print("Shutdown all nodes")
    for node in [node0, node1, node2, node3, node4]:
        node.kill(signal.SIGTERM)

    for node in [node0, node1, node2, node3, node4]:
        assert not node.verifyAlive(), "Node did not shutdown"

    Print("Remove reversible blocks and state of all nodes")
    for node in [node0, node1, node2, node3, node4]:
        node.removeReversibleBlks()
        node.removeState()

    Print("Restart all nodes and verify LIB advances")
    for i in range(5):
        isRelaunchSuccess = cluster.getNode(i).relaunch(chainArg=" -e --snapshot {}".format(node0.getLatestSnapshot()))
        assert isRelaunchSuccess, f"node {i} relaunch from snapshot failed"

    # When node0 is launched there will be no nodes to connect to. The default 30 second connection timer will have
    # to fire first, provide 60 seconds instead of the default 30 seconds to connect and for LIB to advance.
    for node in [node4, node3, node2, node1, node0]:
        assert node.waitForLibToAdvance(timeout=60), f"Node {node.nodeId} did not advance LIB after relaunch"
        lib = node.getIrreversibleBlockNum()
        assert lib > libN, f"Node LIB {lib} <= LIB N {libN}"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
