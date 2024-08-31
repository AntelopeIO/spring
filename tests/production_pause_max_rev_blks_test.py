#!/usr/bin/env python3
import os
import shutil
import signal
import time

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

####################################################################################
# production_pause_max_rev_blks_test
# Test --max-reversible-blocks works as expected.
#
# Setup:
#
# The test network consits of 3 nodes. 2 of them are producer nodes, with
# producera and producerb. Configure --max-reversible-blocks but
# disable --production-pause-vote-timeout-ms to avoid interfernce.
#
# node0: Enables block production for producera and has the finalizer key with
#        description of producera. Has vote-threads enabled. Connect to producerbNode.
# producerbNode: Enables block production for producerb. Has vote-threads enabled.
#        Connect to finalizerbNode and node0.
# finalizerbNode: Has the finalizer key with description of producerb.
#        Has vote-threads enabled. Connect to producerbNode.
#
# Test cases:
#
# 1. Bring down finalizerbNode. producerbNode and node0 should eventually
#    automatically pause production due to not receiving votes from finalizerbNode,
#    LIB stopping advancing, and number of reversible blocks keeping growing
#    on both production nodes.
# 2. Bring down node0 and restart it with a larger --max-reversible-blocks.
#    node0 should resume production until the new limit is reached.
# 3. Restart finalizerbNode. producerbNode and node0 should automatically unpause
#    production because the number of reversible blocks decreases due to LIB
#    advances on both nodes.
####################################################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-d","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"})
delay=args.d
debug=args.v
dumpErrorDetails=args.dump_error_details
pnodes=2 # number of producing nodes. producers are defproducera and defproducerb
totalNodes=pnodes + 1 # plus 1 finalizer node for defproducerb
prodCount=1 # number of producers per producing node

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, keepRunning=args.leave_running, keepLogs=args.keep_logs)

try:
    ####################### set up  ######################
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    # Set --max-reversible-blocks to a small number so it is quick to be hit
    # and disable production-pause-vote-timeout so that production pause is only
    # caused by max-reversible-blocks reached.
    extraNodeosArgs="--max-reversible-blocks 48 --production-pause-vote-timeout-ms 0"

    Print("Stand up cluster")
    # Cannot use activateIF to transition to Savanna directly as it assumes
    # each producer node has finalizer configured.
    if cluster.launch(pnodes=pnodes, totalNodes=totalNodes,
                      totalProducers=pnodes, prodCount=prodCount,
                      delay=delay, loadSystemContract=False,
                      extraNodeosArgs=extraNodeosArgs,
                      activateIF=False, signatureProviderForNonProducer=True,
                      topo="./tests/production_pause_max_rev_blks_test_shape.json") is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"

    node0          = cluster.getNode(0)   # producer and finalizer node for defproducera
    producerbNode  = cluster.getNode(1)   # producer node for defproducerb
    finalizerbNode = cluster.getNode(2)   # finalizer node for defproducerb

    Print("Set finalizer policy and start transition to Savanna")
    # Specifically, need to configure finalizer name for finalizerbNode as defproducerb
    transId = cluster.setFinalizers(nodes=[node0, finalizerbNode], finalizerNames=["defproducera", "defproducerb"])
    assert transId is not None, "setfinalizers failed"
    assert cluster.biosNode.waitForTransFinalization(transId), f"setfinalizers transaction {transId} was not rolled into a LIB block"
    assert cluster.biosNode.waitForLibToAdvance(), "LIB did not advance after setFinalizers"

    # biosNode no longer needed
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    Print("Wait for LIB on all producing nodes to advance")
    assert node0.waitForLibToAdvance(), "node0 did not advance LIB"
    assert producerbNode.waitForLibToAdvance(), "producerbNode did not advance LIB"

    ####################### test 1 ######################

    Print("Shutdown finalizerbNode")
    finalizerbNode.kill(signal.SIGTERM)
    assert not finalizerbNode.verifyAlive(), "finalizerbNode did not shutdown"

    time.sleep(24)

    # Verify producerbNode paused
    assert producerbNode.paused(), "producerbNode still producing after finalizerbNode was shutdown"
    # Verify node0 and node1 still producing but LIB should not advance
    assert node0.paused(), "node0 still producing after finalizerbNode was shutdown"
    if node0.waitForLibToAdvance(timeout=5): # LIB can advance for a few blocks first
        assert not node0.waitForLibToAdvance(timeout=5), "LIB should not advance on node0 after finalizerbNode was shutdown"

    ####################### test 2 ######################
    node0.getInfo()
    prevHeadBlockNum=node0.lastRetrievedHeadBlockNum

    Print("Shutdown node0")
    node0.kill(signal.SIGTERM)
    assert not node0.verifyAlive(), "node0 did not shutdown"

    addSwapFlags={"--max-reversible-blocks": "96"}
    Print("Restart node0")
    node0.relaunch(chainArg="--enable-stale-production", addSwapFlags=addSwapFlags)

    time.sleep(7)
    node0.getInfo()
    assert node0.lastRetrievedHeadBlockNum > prevHeadBlockNum, f'node0 head {node0.lastRetrievedHeadBlockNum} did not advance from previous {prevHeadBlockNum}'

    ####################### test 3 ######################

    Print("Restart finalizerbNode")
    finalizerbNode.relaunch()

    Print("Verify production unpaused and LIB advances after restart of finalizerbNode")
    assert node0.waitForLibToAdvance(), "node0 did not advance LIB"
    assert producerbNode.waitForLibToAdvance(), "producerbNode did not advance LIB"
    time.sleep(13)
    assert not node0.paused(), "node0 should have resumed production after finalizerbNode restarted"
    assert not producerbNode.paused(), "producerbNode should have resumed production after finalizerbNode restarted"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
