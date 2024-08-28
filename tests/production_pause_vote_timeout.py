#!/usr/bin/env python3
import os
import shutil
import signal
import time

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

####################################################################################
# production_pause_vote_timeout
# Test production-pause-vote-timeout works as expected.
#
# Setup:
#
# Use five nodes in an hourglass topology. The center node is a relay node that
# initially has vote-threads enabled. The other 4 peripheral nodes are:
#
# node0: Enables block production for producera and has the finalizer key with
#        description of producera. Has vote-threads enabled. Connect to the center node.
# node1: Enables block production for producerb and has the finalizer key with
#        description of producerb. Has vote-threads enabled. Connect to the center node
#        and Node1.
# producercNode: Enables block production for producerc.
# finalizercNode: Has the finalizer key with description of producerc.
#        Has vote-threads enabled. Connect to the center node and producercNode.
# 
# Test cases:
#
# 1. Bring down finalizercNode. producercNode should eventually
#    automatically pause production due to not receiving votes from finalizercNode.
#    that are associated to its producerc. However, Node0 and Node1 should not pause.
#    Then bring finalizercNode. back up. producercNode should
#    automatically resume production.
# 2. Bring down the center node. producercNode should eventually automatically
#    pause production due to not receiving votes from Node0 and Node1 that are
#    associated with the other producers. However, Node0 and Node1 should not pause.
#    Then bring the center node back up. producercNode should automatically
#    resume production.
# 3. Restart producercNode with "--production-pause-vote-timeout-ms 0" to
#    disable production-pause-vote-timeout. Bring down finalizercNode.
#    producercNode should keep producing.
#
####################################################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-d","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"})
delay=args.d
debug=args.v
dumpErrorDetails=args.dump_error_details
pnodes=3 # number of producing nodes
totalNodes=pnodes + 2 # plus 1 center node and 1 finalizer node for defproducerc
prodCount=1 # number of producers per producing node

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, keepRunning=args.leave_running, keepLogs=args.keep_logs)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    # for defproducerc producing node
    specificExtraNodeosArgs={}
    specificExtraNodeosArgs[2]="--production-pause-vote-timeout-ms 1000"

    Print("Stand up cluster")
    # Cannot use activateIF to transition to Savanna directly as it assumes
    # each producer node has finalizer configured.
    if cluster.launch(pnodes=pnodes, totalNodes=totalNodes, totalProducers=pnodes, prodCount=prodCount, delay=delay, loadSystemContract=False,
                      specificExtraNodeosArgs=specificExtraNodeosArgs,
                      activateIF=False, signatureProviderForNonProducer=True,
                      topo="./tests/production_pause_vote_timeout_test_shape.json") is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"

    node0          = cluster.getNode(0)   # producer and finalizer node for defproducera
    node1          = cluster.getNode(1)   # producer and finalizer node for defproducerb
    producercNode  = cluster.getNode(2)   # producer node for defproducerc
    finalizercNode = cluster.getNode(3)   # finalizer node for defproducerc
    centerNode     = cluster.getNode(4)

    Print("Set finalizer policy and start transition to Savanna")
    # Specifically, need to configure finalizer name for finalizercNode as defproducerc
    transId = cluster.setFinalizers(nodes=[node0, node1, finalizercNode], finalizerNames=["defproducera", "defproducerb", "defproducerc"])
    assert transId is not None, "setfinalizers failed"
    assert cluster.biosNode.waitForTransFinalization(transId), f"setfinalizers transaction {transId} was not rolled into a LIB block"
    assert cluster.biosNode.waitForLibToAdvance(), "LIB did not advance after setFinalizers"

    # biosNode no longer needed
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    Print("Wait for LIB on all producing nodes to advance")
    assert node0.waitForLibToAdvance(), "node0 did not advance LIB"
    assert node1.waitForLibToAdvance(), "node1 did not advance LIB"
    assert producercNode.waitForLibToAdvance(), "producercNode did not advance LIB"

    ####################### test 1 ######################

    Print("Shutdown finalizercNode")
    finalizercNode.kill(signal.SIGTERM)
    assert not finalizercNode.verifyAlive(), "finalizercNode did not shutdown"

    # wait some time for producercNode paused
    paused = False
    for i in range(0, 15):
        time.sleep(1)
        paused = not producercNode.waitForHeadToAdvance(timeout=1)
        if paused:
            Print(f'paused after {i} seconds after finalizercNode was shutdown')
            break;
    # Verify producercNode paused
    assert paused, "producercNode still producing after finalizercNode was shutdown"
    # Verify node0 and node1 still producing
    assert node0.waitForHeadToAdvance(), "node0 paused after finalizercNode was shutdown"
    assert node1.waitForHeadToAdvance(), "node1 paused after finalizercNode was shutdown"

    Print("Restart finalizercNode")
    finalizercNode.relaunch()

    Print("Verify LIB advances after restart of finalizercNode")
    assert node0.waitForLibToAdvance(), "node0 did not advance LIB"
    assert node1.waitForLibToAdvance(), "node1 did not advance LIB"
    assert producercNode.waitForLibToAdvance(), "producercNode did not advance LIB"

    ####################### test 2 ######################

    Print("Shutdown centerNode")
    centerNode.kill(signal.SIGTERM)
    assert not centerNode.verifyAlive(), "centerNode did not shutdown"

    # wait some time for producercNode paused
    paused = False
    for i in range(0, 15):
        time.sleep(1)
        paused = not producercNode.waitForHeadToAdvance(timeout=1)
        if paused:
            Print(f'paused after {i} seconds after centerNode was shutdown')
            break;
    # Verify producercNode paused
    assert paused, "producercNode still producing after centerNode was shutdown"
    # Verify node0 and node1 still producing
    assert node0.waitForHeadToAdvance(), "node0 paused after centerNode was shutdown"
    assert node1.waitForHeadToAdvance(), "node1 paused after centerNode was shutdown"

    Print("Restart centerNode")
    centerNode.relaunch()

    Print("Verify LIB advances after restart")
    assert node0.waitForLibToAdvance(), "node0 did not advance LIB"
    assert node1.waitForLibToAdvance(), "node1 did not advance LIB"
    assert producercNode.waitForLibToAdvance(), "producercNode did not advance LIB"

    ####################### test 3 ######################

    Print("Shutdown producercNode")
    producercNode.kill(signal.SIGTERM)
    assert not producercNode.verifyAlive(), "producercNode did not shutdown"

    # disable production-pause-vote-timeout
    Print("Relaunch producercNode with --production-pause-vote-timeout-ms 0")
    addSwapFlags={"--production-pause-vote-timeout-ms": "0"}
    producercNode.relaunch(chainArg="--enable-stale-production", addSwapFlags=addSwapFlags)

    Print("Shutdown finalizercNode")
    finalizercNode.kill(signal.SIGTERM)
    assert not finalizercNode.verifyAlive(), "finalizercNode did not shutdown"

    # Verify producercNode still producing
    assert producercNode.waitForHeadToAdvance(), "producercNode (--production-pause-vote-timeout-ms 0) paused after finalizercNode was shutdown"
    # Verify node0 and node1 still producing
    assert node0.waitForHeadToAdvance(), "node0 paused after finalizercNode was shutdown"
    assert node1.waitForHeadToAdvance(), "node1 paused after finalizercNode was shutdown"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
