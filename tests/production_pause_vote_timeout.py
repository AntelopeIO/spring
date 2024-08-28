#!/usr/bin/env python3
import os
import shutil
import signal
import time

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

###############################################################
# production_pause_vote_timeout
# Test production-pause-vote-timeout works as expected.
#
# Setup:
#
# Use five nodes in an hourglass topology. The center node is a relay node that
# initially has vote-threads enabled. The other 4 peripheral nodes are:
#
# Node0: Enables block production for producera and has the finalizer key with
#        description of producera. Has vote-threads enabled. Connect to the center node.
# Node1: Enables block production for producerb and has the finalizer key with
#        description of producerb. Has vote-threads enabled. Connect to the center node
#        and Node1.
# defproducercProducerNode: Enables block production for producerc.
# defproducercFinalizerNode: Has the finalizer key with description of producerc.
#        Has vote-threads enabled. Connect to the center node and defproducercProducerNode.
# 
# Test cases:
#
# 1. Bring down defproducercFinalizerNode. defproducercProducerNode should eventually
#    automatically pause production due to not receiving votes from defproducercFinalizerNode.
#    that are associated to its producerc. However, Node0 and Node1 should not pause.
#    Then bring defproducercFinalizerNode. back up. defproducercProducerNode should
#    automatically resume production.
# 2. Bring down the center node. defproducercProducerNode should eventually automatically
#    pause production due to not receiving votes from Node0 and Node1 that are
#    associated with the other producers. However, Node0 and Node1 should not pause.
#    Then bring the center node back up. defproducercProducerNode should automatically
#    resume production.
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-d","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"})
delay=args.d
debug=args.v
dumpErrorDetails=args.dump_error_details
pnodes=3
totalNodes=pnodes + 2
prodCount=1

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, keepRunning=args.leave_running, keepLogs=args.keep_logs)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    specificExtraNodeosArgs={}
    specificExtraNodeosArgs[2]="--production-pause-vote-timeout-ms 1000"
    Print("Stand up cluster")
    if cluster.launch(pnodes=pnodes, totalNodes=totalNodes, totalProducers=pnodes, prodCount=prodCount, delay=delay, loadSystemContract=False,
                      specificExtraNodeosArgs=specificExtraNodeosArgs,
                      activateIF=False, signatureProviderForNonProducer=True,
                      topo="./tests/production_pause_vote_timeout_test_shape.json") is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"

    node0 = cluster.getNode(0)                     # producer and finalizer node for defproducera
    node1 = cluster.getNode(1)                     # producer and finalizer node for defproducerb
    defproducercProducerNode = cluster.getNode(2)  # producer node for defproducerc
    defproducercFinalizerNode = cluster.getNode(3) # finalizer node for defproducerc
    centerNode = cluster.getNode(4)

    Print("Set finalizer policy and start transition to Savanna")
    transId = cluster.setFinalizers(nodes=[node0, node1, defproducercFinalizerNode], finalizerNames=["defproducera", "defproducerb", "defproducerc"])
    assert transId is not None, "setfinalizers failed"
    if not cluster.biosNode.waitForTransFinalization(transId):
        Print(f'ERROR: setfinalizers transaction {transId} was not rolled into a LIB block')
    assert cluster.biosNode.waitForLibToAdvance(), "LIB did not advance after setFinalizers"

    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    Print("Wait for lib to advance")
    assert node0.waitForLibToAdvance(), "node0 did not advance LIB"
    assert node1.waitForLibToAdvance(), "node1 did not advance LIB"
    assert defproducercProducerNode.waitForLibToAdvance(), "defproducercProducerNode did not advance LIB"

    Print("Shutdown producer defproducercFinalizerNode")
    defproducercFinalizerNode.kill(signal.SIGTERM)
    assert not defproducercFinalizerNode.verifyAlive(), "defproducercFinalizerNode did not shutdown"

    # production-pause-vote-timeout was set to 1 second. wait for at most 15 seconds
    paused = False
    for i in range(0, 15):
        time.sleep(1)
        paused = not defproducercProducerNode.waitForHeadToAdvance(timeout=1)
        if paused:
            Print(f'paused after {i} seconds since defproducercProducerNode shutdown')
            break;
    assert paused, "defproducercProducerNode still producing after defproducercFinalizerNode was shutdown"

    # node0 and node1 still producing
    assert node0.waitForHeadToAdvance(), "node0 paused after defproducercFinalizerNode was shutdown"
    assert node1.waitForHeadToAdvance(), "node1 paused after defproducercFinalizerNode was shutdown"

    Print("Restart producer defproducercFinalizerNode")
    defproducercFinalizerNode.relaunch()

    Print("Verify LIB advances after restart")
    assert node0.waitForLibToAdvance(), "node0 did not advance LIB"
    assert node1.waitForLibToAdvance(), "node1 did not advance LIB"
    assert defproducercProducerNode.waitForLibToAdvance(), "defproducercProducerNode did not advance LIB"

    Print("Shutdown centerNode")
    centerNode.kill(signal.SIGTERM)
    assert not centerNode.verifyAlive(), "centerNode did not shutdown"

    # wait for at most 15 seconds
    paused = False
    for i in range(0, 15):
        time.sleep(1)
        paused = not defproducercProducerNode.waitForHeadToAdvance(timeout=1)
        if paused:
            Print(f'paused after {i} seconds since centerNode shutdown')
            break;
    assert paused, "defproducercProducerNode still producing after centerNode was shutdown" 

    # node0 and node1 still producing
    assert node0.waitForHeadToAdvance(), "node0 paused after centerNode was shutdown"
    assert node1.waitForHeadToAdvance(), "node1 paused after centerNode was shutdown"

    Print("Restart centerNode")
    centerNode.relaunch()

    Print("Verify LIB advances after restart")
    assert node0.waitForLibToAdvance(), "node0 did not advance LIB"
    assert node1.waitForLibToAdvance(), "node1 did not advance LIB"
    assert defproducercProducerNode.waitForLibToAdvance(), "defproducercProducerNode did not advance LIB"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
