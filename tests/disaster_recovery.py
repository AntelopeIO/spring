#!/usr/bin/env python3
import os
import shutil
import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

###############################################################
# disaster_recovery
#
# Integration test with 4 finalizers (A, B, C, and D).
#
#   The 4 nodes are cleanly shutdown in the following state:
#   - A has LIB N. A has a finalizer safety information file that locks on a block after N.
#   - B, C, and D have LIB less than N. They have finalizer safety information files that lock on N.
#
#   All nodes lose their reversible blocks and restart from an earlier snapshot.
#
#   A is restarted and replays up to block N after restarting from snapshot. Block N is sent to the other
#   nodes B, C, and D after they are also started up again.
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

def removeReversibleBlks(nodeId):
   dataDir = Utils.getNodeDataDir(nodeId)
   reversibleBlks = os.path.join(dataDir, "blocks", "reversible")
   shutil.rmtree(reversibleBlks, ignore_errors=True)

def removeState(nodeId):
   dataDir = Utils.getNodeDataDir(nodeId)
   state = os.path.join(dataDir, "state")
   shutil.rmtree(state, ignore_errors=True)

def getLatestSnapshot(nodeId):
    snapshotDir = os.path.join(Utils.getNodeDataDir(nodeId), "snapshots")
    snapshotDirContents = os.listdir(snapshotDir)
    assert len(snapshotDirContents) > 0
    snapshotDirContents.sort()
    return os.path.join(snapshotDir, snapshotDirContents[-1])

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    Print("Stand up cluster")
    # For now do not load system contract as it does not support setfinalizer
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, prodCount=prod_count, delay=delay, loadSystemContract=False,
                      activateIF=True) is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    node0 = cluster.getNode(0)
    node1 = cluster.getNode(1)
    node2 = cluster.getNode(2)
    node3 = cluster.getNode(3)

    Print("Create snapshot (node 0)")
    ret = node0.createSnapshot()
    assert ret is not None, "Snapshot creation failed"
    ret_head_block_num = ret["payload"]["head_block_num"]
    Print(f"Snapshot head block number {ret_head_block_num}")

    Print("Wait for snapshot node lib to advance")
    node0.waitForBlock(ret_head_block_num+1, blockType=BlockType.lib)
    node0.waitForLibToAdvance()
    node1.waitForLibToAdvance()

    node1.kill(signal.SIGTERM)
    node2.kill(signal.SIGTERM)
    node3.kill(signal.SIGTERM)

    assert not node1.verifyAlive(), "Node1 did not shutdown"
    assert not node2.verifyAlive(), "Node2 did not shutdown"
    assert not node3.verifyAlive(), "Node3 did not shutdown"

    node0.waitForHeadToAdvance()
    node0.kill(signal.SIGTERM)
    assert not node0.verifyAlive(), "Node0 did not shutdown"

    removeReversibleBlks(0)
    removeReversibleBlks(1)
    removeReversibleBlks(2)
    removeReversibleBlks(3)
    removeState(0)
    removeState(1)
    removeState(2)
    removeState(3)

    isRelaunchSuccess = node0.relaunch(chainArg=" --snapshot {}".format(getLatestSnapshot(0)))
    assert isRelaunchSuccess, "node 0 relaunch from snapshot failed"
    isRelaunchSuccess = node1.relaunch(chainArg=" --snapshot {}".format(getLatestSnapshot(0)))
    assert isRelaunchSuccess, "node 1 relaunch from snapshot failed"
    isRelaunchSuccess = node2.relaunch(chainArg=" --snapshot {}".format(getLatestSnapshot(0)))
    assert isRelaunchSuccess, "node 2 relaunch from snapshot failed"
    isRelaunchSuccess = node3.relaunch(chainArg=" --snapshot {}".format(getLatestSnapshot(0)))
    assert isRelaunchSuccess, "node 3 relaunch from snapshot failed"

    node0.waitForLibToAdvance()
    node1.waitForLibToAdvance()
    node2.waitForLibToAdvance()
    node3.waitForLibToAdvance()

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
