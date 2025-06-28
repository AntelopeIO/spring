#!/usr/bin/env python3

import json
import signal

from TestHarness import Account, Cluster, Node, ReturnType, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# pause_at_block_test
#
# Verify /v1/producer/pause_at_block pauses node in all read modes
#
###############################################################

# Parse command line arguments
args = TestHelper.parse_args({"-v","--dump-error-details","--leave-running","--keep-logs","--unshared"})
Utils.Debug = args.v
dumpErrorDetails=args.dump_error_details
dontKill=args.leave_running
keepLogs=args.keep_logs

walletMgr=WalletMgr(True)
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
cluster.setWalletMgr(walletMgr)

testSuccessful = False
try:
    TestHelper.printSystemInfo("BEGIN")

    specificNodeosArgs = {
        0 : "--enable-stale-production",

        3 : "--read-mode head",
        4 : "--read-mode speculative",
        5 : "--read-mode irreversible"
    }
    assert cluster.launch(
        pnodes=3,
        prodCount=3,
        totalProducers=3,
        totalNodes=6,
        loadSystemContract=False,
        activateIF=True)

    prodNode = cluster.getNode(0)
    prodNode2 = cluster.getNode(1)
    headNode = cluster.getNode(3)
    specNode = cluster.getNode(4)
    irrvNode = cluster.getNode(5)

    prodNode.waitForProducer("defproducerb");
    prodNode.waitForProducer("defproducera");

    blockNum = prodNode.getHeadBlockNum()

    blockNum += 5

    Utils.Print(f"Pausing at block {blockNum}")
    prodNode2.processUrllibRequest("producer", "pause_at_block", {"block_num":blockNum}),
    headNode.processUrllibRequest("producer", "pause_at_block", {"block_num":blockNum}),
    specNode.processUrllibRequest("producer", "pause_at_block", {"block_num":blockNum}),
    irrvNode.processUrllibRequest("producer", "pause_at_block", {"block_num":blockNum}),

    assert prodNode.waitForLibToAdvance(), "LIB did not advance with paused nodes"
    assert prodNode2.waitForBlock(blockNum), f"Block {blockNum} did not arrive after pausing"
    assert headNode.waitForBlock(blockNum), f"Block {blockNum} did not arrive after pausing"
    assert specNode.waitForBlock(blockNum), f"Block {blockNum} did not arrive after pausing"
    assert irrvNode.waitForBlock(blockNum), f"Block {blockNum} did not arrive after pausing"

    Utils.Print(f"Verify paused at block {blockNum}")
    assert prodNode2.getHeadBlockNum() == blockNum, "Prod Node_01 did not pause at block"
    assert headNode.getHeadBlockNum() == blockNum, "Head Node_03 did not pause at block"
    assert specNode.getHeadBlockNum() == blockNum, "Speculative Node_04 did not pause at block"
    assert irrvNode.getHeadBlockNum() == blockNum, "Irreversible Node_05 did not pause at block"

    Utils.Print(f"Verify prod node still producing blocks")
    assert prodNode.waitForLibToAdvance(), "LIB did not advance with paused nodes"

    Utils.Print(f"Verify still paused at block {blockNum}")
    assert prodNode2.getHeadBlockNum() == blockNum, "Prod Node_01 did not pause at block"
    assert headNode.getHeadBlockNum() == blockNum, "Head Node_03 did not pause at block"
    assert specNode.getHeadBlockNum() == blockNum, "Speculative Node_04 did not pause at block"
    assert irrvNode.getHeadBlockNum() == blockNum, "Irreversible Node_05 did not pause at block"

    Utils.Print(f"Resume paused nodes")
    prodNode2.processUrllibRequest("producer", "resume", {})
    headNode.processUrllibRequest("producer", "resume", {})
    specNode.processUrllibRequest("producer", "resume",{})
    irrvNode.processUrllibRequest("producer", "resume", {})

    Utils.Print(f"Verify nodes resumed")
    assert prodNode2.waitForLibToAdvance(), "Prod Node_01 did not resume"
    assert headNode.waitForLibToAdvance(), "Head Node_03 did not resume"
    assert specNode.waitForLibToAdvance(), "Speculative Node_04 did not resume"
    assert irrvNode.waitForLibToAdvance(), "Irreversible Node_05 did not resume"

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
