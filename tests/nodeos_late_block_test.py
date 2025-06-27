#!/usr/bin/env python3
import os
import shutil
import signal
import time
from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

###############################################################
# nodeos_late_block_test
#
# Set up a cluster of 4 producer nodes so that 3 can reach consensus.
# Node_00 - defproducera,b,c
# Node_01 - defproducerd,e,f
# Node_02 - defproducerg,h,i
#  Node_04 - bridge between 2 & 3
# Node_03 - defproducerj,k,l
#
# When Node_02 is producing shutdown Node_04 and bring it back up when Node_03 is producing.
# Verify that Node_03 realizes it should switch over to fork other nodes have chosen.
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-d","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"})
pnodes=4
total_nodes=pnodes + 1
delay=args.d
debug=args.v
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
    # do not allow pause production to interfere with late block test
    extraNodeosArgs=" --production-pause-vote-timeout-ms 0 "

    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, extraNodeosArgs=extraNodeosArgs,
                      topo="./tests/nodeos_late_block_test_shape.json", delay=delay, loadSystemContract=False,
                      activateIF=True, signatureProviderForNonProducer=True) is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    node3 = cluster.getNode(3)
    node4 = cluster.getNode(4) # bridge between 2 & 3

    Print("Wait for producer before j")
    node3.waitForAnyProducer("defproducerh", exitOnError=True)
    node3.waitForAnyProducer("defproduceri", exitOnError=True)
    iProdBlockNum = node3.getHeadBlockNum()

    node4.kill(signal.SIGTERM)
    assert not node4.verifyAlive(), "Node4 did not shutdown"

    Print("Wait until Node_03 starts to produce its second round ")
    node3.waitForProducer("defproducerk", exitOnError=True)

    Print("Relaunch bridge to connection Node_02 and Node_03")
    node4.relaunch()

    Print("Verify Node_03 fork switches even though it is producing")
    node3.waitForProducer("defproduceri", exitOnError=True)
    Print("Verify fork switch")
    assert node3.findInLog("switching forks .* defproducerk"), "Expected to find 'switching forks' in node_03 log"

    Print("Wait until Node_00 to produce")
    node3.waitForProducer("defproducera")

    # verify the LIB blocks of defproduceri made it into the canonical chain
    # defproducerk has produced at least one block, but possibly more by time of relaunch, so verify only some of the round
    for i in range(9):
        defprod=node3.getBlockProducerByNum(iProdBlockNum + i)
        assert defprod == "defproduceri", f"expected defproduceri for block {iProdBlockNum + i}, instead: {defprod}"

    # verify that defproducerk blocks made it into the canonical chain as well
    # It can take a while to resolve the fork, but should have at least one defproducerk block unless defproducerl
    # wins the fork in which case there will be another fork switch
    expectedProd = "defproducerk"
    if node3.findInLog("switching forks .* defproducerl"):
        expectedProd = "defproducera"
    iProdBlockNum += 12 # into the next set of blocks
    found_defproducer = False
    for i in range(12):
        defprod=node3.getBlockProducerByNum(iProdBlockNum + i)
        if defprod == expectedProd:
            found_defproducer = True

    assert found_defproducer, f"expected {expectedProd} in blocks {iProdBlockNum}-{iProdBlockNum+12}"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
