#!/usr/bin/env python3
import os
import shutil
import signal
import time

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

###############################################################
# production_pause_vote_timeout
#
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

    node0 = cluster.getNode(0)
    node1 = cluster.getNode(1)
    node2 = cluster.getNode(2)
    node3 = cluster.getNode(3)
    centerNode = cluster.getNode(4)

    Print("Set finalizer policy and start transition to Savanna")
    transId = cluster.setFinalizers(nodes=[node0, node1, node3], finalizerNames=["defproducera", "defproducerb", "defproducerc"])
    assert transId is not None, "setfinalizers failed"
    if not cluster.biosNode.waitForTransFinalization(transId):
        Print(f'ERROR: setfinalizers transaction {transId} was not rolled into a LIB block')
    assert cluster.biosNode.waitForLibToAdvance(), "LIB did not advance after setFinalizers"

    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    Print("Wait for lib to advance")
    assert node0.waitForLibToAdvance(), "Node0 did not advance LIB"
    assert node1.waitForLibToAdvance(), "Node1 did not advance LIB"
    assert node2.waitForLibToAdvance(), "Node2 did not advance LIB"

    Print("Shutdown producer node3")
    node3.kill(signal.SIGTERM)
    assert not node3.verifyAlive(), "Node3 did not shutdown"

    # production-pause-vote-timeout was set to 1 second. wait for at most 15 seconds
    paused = False
    for i in range(0, 15):
        time.sleep(1)
        paused = not node2.waitForHeadToAdvance(timeout=1)
        if paused:
            Print(f'paused after {i} seconds since node2 shutdown')
            break;
    assert paused, "node2 head still advancing after node3 was shutdown" 

    # Lib on node0 and node1 still advance
    assert node0.waitForHeadToAdvance(), "node0 paused after node3 was shutdown"
    assert node1.waitForHeadToAdvance(), "node1 paused after node3 was shutdown"

    Print("Restart producer node3")
    node3.relaunch()

    Print("Verify LIB advances after restart")
    assert node0.waitForLibToAdvance(), "Node0 did not advance LIB"
    assert node1.waitForLibToAdvance(), "Node1 did not advance LIB"
    assert node2.waitForLibToAdvance(), "Node2 did not advance LIB"

    Print("Shutdown centerNode")
    centerNode.kill(signal.SIGTERM)
    assert not centerNode.verifyAlive(), "centerNode did not shutdown"

    # wait for at most 15 seconds
    paused = False
    for i in range(0, 15):
        time.sleep(1)
        paused = not node2.waitForHeadToAdvance(timeout=1)
        if paused:
            Print(f'paused after {i} seconds since centerNode shutdown')
            break;
    assert paused, "Node2 head still advancing after centerNode was shutdown" 

    # Lib on node0 and node1 still advance
    assert node0.waitForHeadToAdvance(), "node0 paused after centerNode was shutdown"
    assert node1.waitForHeadToAdvance(), "node1 paused after centerNode was shutdown"

    Print("Restart centerNode")
    centerNode.relaunch()

    Print("Verify LIB advances after restart")
    assert node0.waitForLibToAdvance(), "Node0 did not advance LIB"
    assert node1.waitForLibToAdvance(), "Node1 did not advance LIB"
    assert node2.waitForLibToAdvance(), "Node2 did not advance LIB"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
