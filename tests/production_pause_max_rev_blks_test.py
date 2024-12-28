#!/usr/bin/env python3
import os
import shutil
import signal
import time

from TestHarness import Cluster, TestHelper, Utils, WalletMgr

####################################################################################
# production_pause_max_rev_blks_test
# Test --max-reversible-blocks works as expected.
#
# Setup:
#
# The test network consits of 3 nodes. 2 of them are producer nodes, with
# producera and producerb. Configure --max-reversible-blocks but
# disable --production-pause-vote-timeout-ms to avoid interference.
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
# 1. Bring down finalizerbNode. producerbNode and node0 should automatically
#    pause production due to the number of reversible blocks keeping growing
#    because not receiving votes from finalizerbNode and LIB stopping advancings.
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
totalNodes=pnodes + 1 # 1 is for the finalizer node of defproducerb
prodCount=1 # number of producers per producing node

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, keepRunning=args.leave_running, keepLogs=args.keep_logs)

try:
    ####################### setting up  ######################
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, totalNodes: {totalNodes}, prodCount: {prodCount}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    # Do not configure --max-reversible-blocks before transition to Savanna
    # but it needs to be big enough while in Legacy.
    # Disable production-pause-vote-timeout so that production pause is only
    # caused by max-reversible-blocks reached.
    extraNodeosArgs="--production-pause-vote-timeout-ms 0"

    Print("Stand up cluster")
    # Cannot use activateIF to transition to Savanna directly as it assumes
    # each producer node has finalizer configured.
    # In this test, node1 does not have finalizer configured.
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

    Print("Restart node0 and producerbNode with max-reversible-blocks")
    node0.kill(signal.SIGTERM)
    assert not node0.verifyAlive(), "node0 did not shutdown"
    producerbNode.kill(signal.SIGTERM)
    assert not producerbNode.verifyAlive(), "producerbNode did not shutdown"
    maxReversibleBlocks=6
    addSwapFlags={"--max-reversible-blocks": str(maxReversibleBlocks)}
    node0.relaunch(chainArg="--enable-stale-production", addSwapFlags=addSwapFlags)
    producerbNode.relaunch(chainArg="--enable-stale-production", addSwapFlags=addSwapFlags)

    ####################### test 1 ######################
    # production paused due to max reversible blocks exceeded

    Print("Shutdown finalizerbNode")
    finalizerbNode.kill(signal.SIGTERM)
    assert not finalizerbNode.verifyAlive(), "finalizerbNode did not shutdown"

    # Verify LIB stalled on node0 and producerbNode due to finalizerNode was shutdown
    Print("Verify LIB stalled after shutdown of finalizerbNode")
    # LIB can advance for a few blocks first
    assert producerbNode.waitForLibNotToAdvance(timeout=10), "LIB should not advance on producerbNode after finalizerbNode was shutdown"
    assert node0.waitForLibNotToAdvance(timeout=10), "LIB should not advance on node0 after finalizerbNode was shutdown"

    # Wait until enough reversible blocks are produced
    node0.getInfo()
    node0.waitForBlock(node0.lastRetrievedLIB + maxReversibleBlocks)

    # Verify node0 and producerbNode then paused due to max reversible blocks exceeded
    Print("Verify production paused after LIB stalled")
    assert producerbNode.missedNextProductionRound(), "producerbNode still producing after finalizerbNode was shutdown"
    assert node0.missedNextProductionRound(), "node0 still producing after finalizerbNode was shutdown"

    ####################### test 2 ###########################
    # nodeos can restart with a higher --max-reversible-blocks

    node0.getInfo()
    prevHeadBlockNum=node0.lastRetrievedHeadBlockNum

    Print("Shutdown node0")
    node0.kill(signal.SIGTERM)
    assert not node0.verifyAlive(), "node0 did not shutdown"

    higherMaxReversibleBlocks=maxReversibleBlocks+10 # 10 is just a value chosen for test
    addSwapFlags={"--max-reversible-blocks": str(higherMaxReversibleBlocks)}
    Print("Restart node0 with a higher max-reversible-blocks")
    node0.relaunch(addSwapFlags=addSwapFlags) # enable-stale-production was already configured in last relaunch

    node0.waitForBlock(prevHeadBlockNum + 1)

    Print("Verify head block advances after node0 restart")
    node0.getInfo()
    assert node0.lastRetrievedHeadBlockNum > prevHeadBlockNum, f'node0 head {node0.lastRetrievedHeadBlockNum} did not advance from previous {prevHeadBlockNum}'

    ####################### test 3 ######################
    # Restart finalizer node make all nodes unpause production

    Print("Restart finalizerbNode")
    finalizerbNode.relaunch()

    Print("Verify LIB advances after restart of finalizerbNode")
    assert node0.waitForLibToAdvance(), "node0 did not advance LIB"
    assert producerbNode.waitForLibToAdvance(), "producerbNode did not advance LIB"

    # After LIB advances, number of reversible blocks should decrease and
    # as a result production is unpaused
    Print("Verify production unpaused after LIB advances")
    assert not node0.missedNextProductionRound(), "node0 should have unpaused production after LIB advances"
    assert not producerbNode.missedNextProductionRound(), "producerbNode should have unpaused production after LIB advances"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
