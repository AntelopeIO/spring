#!/usr/bin/env python3

import re
import signal
import time

from TestHarness import Cluster, TestHelper, Utils, WalletMgr

###############################################################
# nodeos_read_terminate_at_block_test
#
# A few tests centered around read mode of irreversible, head, and speculative
# with terminate-at-block set for regular and replay from snapshot through  block logs.
#
###############################################################

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
relaunchTimeout = 10
numOfProducers = 1
# One producing node, three regular terminate-at-block nodes, and three nodes
# where replay snapshot through block logs with terminate-at-block
totalNodes = 7

# Parse command line arguments
args = TestHelper.parse_args({
    "-v",
    "--activate-if",
    "--dump-error-details",
    "--leave-running",
    "--keep-logs",
    "--unshared"
})

Utils.Debug = args.v
activateIF=args.activate_if
dumpErrorDetails = args.dump_error_details

# Wrapper function to execute test
# This wrapper function will resurrect the node to be tested, and shut
# it down by the end of the test
def executeTest(cluster, testNodeId, testNodeArgs, resultMsgs):
    testNode = None
    testResult = False
    resultDesc = "!!!BUG IS CONFIRMED ON TEST CASE #{} ({})".format(
        testNodeId,
        testNodeArgs
    )

    try:
        Print(
            "Launch node #{} to execute test scenario: {}".format(
                testNodeId,
                testNodeArgs
            )
        )

        testNode = cluster.getNode(testNodeId)
        assert not testNode.verifyAlive() # resets pid so reluanch works
        testNode.relaunch(addSwapFlags={"--terminate-at-block": "9999999"})

        # Wait for node to start up.
        time.sleep(3)

        # Check the node stops at the correct block.
        checkStatus(testNode, testNodeArgs)

        # Kill node after use.
        if not testNode.killed:
            assert testNode.kill(signal.SIGTERM)

        # Replay the blockchain for the node that just finished,
        # also checking it stops at the correct block.
        checkReplay(testNode, testNodeArgs)

        resultDesc = "!!!TEST CASE #{} ({}) IS SUCCESSFUL".format(
            testNodeId,
            testNodeArgs
        )
        testResult = True

    finally:
        Print(resultDesc)
        resultMsgs.append(resultDesc)

        # Kill node after use.
        if testNode and not testNode.killed:
            assert testNode.kill(signal.SIGTERM)

    return testResult


def checkStatus(testNode, testNodeArgs):
    """Test --terminate-at-block stops at the correct block."""
    Print(" ".join([
        "The test node has begun receiving from the producing node and",
        "is expected to stop at the block number specified here: ",
        testNodeArgs
    ]))

    # Read block information from the test node as it runs.
    head, lib = getBlockNumInfo(testNode)

    Print("Test node head = {}, lib = {}.".format(head, lib))

    if "irreversible" in testNodeArgs:
        checkIrreversible(head, lib)
    else:
        checkHeadOrSpeculative(head, lib)

    # Check for the terminate at block message.
    match = re.search(r"--terminate-at-block (\d+)", testNodeArgs)
    termAtBlock = int(match.group(1))

    assert head == termAtBlock, f"head {head} termAtBlock {termAtBlock}"


def checkReplay(testNode, testNodeArgs):
    """Test --terminate-at-block with --replay-blockchain."""
    # node.getInfo() doesn't work when replaying the blockchain so a
    # relaunch  combined with --terminate-at-block will appear to fail.
    # In reality, the relaunch works fine and it will (hopefully)
    # run until completion normally. Code below ensures it does.
    Print(" ".join([
        "Relaunch the node in replay mode. The replay should stop",
        "at or little bigger than the block number specified here: ",
        testNodeArgs
    ]))

    assert not testNode.verifyAlive()
    testNode.relaunch(chainArg="--replay-blockchain", addSwapFlags={"--terminate-at-block": "9999999"})

    # Wait for node to finish up.
    time.sleep(3)

    # Check for the terminate at block message.
    match = re.search(r"--terminate-at-block (\d+)", testNodeArgs)
    termAtBlock = int(match.group(1))

    head, lib = getBlockNumInfo(testNode)
    assert head == termAtBlock, f"head {head} termAtBlock {termAtBlock}"


def getBlockNumInfo(testNode):
    head = None
    lib = None

    while True:
        info = testNode.getInfo()

        if not info:
            continue

        try:
            head = info["head_block_num"]
            lib = info["last_irreversible_block_num"]
            break

        except KeyError:
            pass

    assert head and lib, "Could not retrieve head and lib with getInfo()"
    return head, lib


def checkIrreversible(head, lib):
    assert head == lib, (
        "Head ({}) should be equal to lib ({})".format(head, lib)
    )


def checkHeadOrSpeculative(head, lib):
    assert head > lib, (
        "Head ({}) should be greater than lib ({})".format(head, lib)
    )


# Test terminate-at-block for replay from snapshot through block logs
def executeSnapshotBlocklogTest(cluster, testNodeId, resultMsgs, nodeArgs, termAtBlock):
    testNode = cluster.getNode(testNodeId)
    testResult = False
    resultDesc = "!!!BUG IS CONFIRMED ON TEST CASE #{} ({})".format(
        testNodeId,
        f"replay block log, {nodeArgs} --terminate-at-block {termAtBlock}"
    )

    # Kill node before use.
    if not testNode.killed:
        assert testNode.kill(signal.SIGTERM)

    # Start from snapshot, replay through block log and terminate at specified block
    chainArg=f'--snapshot {testNode.getLatestSnapshot()} --replay-blockchain --terminate-at-block {termAtBlock}'
    testNode.relaunch(chainArg=chainArg, waitForTerm=True)

    # Check the node stops at the correct block by checking the log.
    errFileName=f"{cluster.nodeosLogPath}/node_{str(testNodeId).zfill(2)}/stderr.txt"
    with open(errFileName) as errFile:
        for line in errFile:
            m=re.search(r"Block ([\d]+) reached configured maximum block", line)
            if m:
                assert int(m.group(1)) == termAtBlock, f"actual terminating block number {m.group(1)} not equal to expected termAtBlock {termAtBlock}"
                resultDesc = f"!!!TEST CASE #{testNodeId} (replay block log, mode {nodeArgs} --terminate-at-block {termAtBlock}) IS SUCCESSFUL"
                testResult = True

    Print(resultDesc)
    resultMsgs.append(resultDesc)

    if testNode and not testNode.killed:
        assert testNode.kill(signal.SIGTERM)

    return testResult

# Setup cluster and it's wallet manager
walletMgr = WalletMgr(True)
cluster = Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
cluster.setWalletMgr(walletMgr)

# List to contain the test result message
testResultMsgs = []
testSuccessful = False
try:
    specificNodeosArgs = {
        0 : "--enable-stale-production"
    }
    regularNodeosArgs = {
        1 : "--read-mode irreversible --terminate-at-block 75",
        2 : "--read-mode head --terminate-at-block 100",
        3 : "--read-mode speculative --terminate-at-block 125"
    }
    replayNodeosArgs = {
        4 : "--read-mode irreversible",
        5 : "--read-mode head",
        6 : "--read-mode speculative"
    }

    # combine all together
    specificNodeosArgs.update(regularNodeosArgs)
    specificNodeosArgs.update(replayNodeosArgs)

    TestHelper.printSystemInfo("BEGIN")
    cluster.launch(
        prodCount=numOfProducers,
        totalProducers=numOfProducers,
        totalNodes=totalNodes,
        pnodes=1,
        topo="mesh",
        activateIF=activateIF,
        specificExtraNodeosArgs=specificNodeosArgs,
    )

    producingNodeId = 0
    producingNode = cluster.getNode(producingNodeId)

    replayTermAt = {}

    # Create snapshots on replay test nodes
    for nodeId in replayNodeosArgs:
        replayNode = cluster.getNode(nodeId)
        ret = replayNode.createSnapshot()
        assert ret is not None, "snapshot creation on node {nodeId} failed"
        headBlockNum = ret["payload"]["head_block_num"]

        # Set replay for 5 blocks
        termAt = headBlockNum + 5
        replayTermAt[nodeId] = termAt

    # wait for all to terminate, needs to be larger than largest terminate-at-block
    # and leave room for snapshot blocks
    producingNode.waitForBlock( 250, timeout=150 )
    cluster.biosNode.kill(signal.SIGTERM)
    producingNode.kill(signal.SIGTERM)

    # Stop all replay nodes
    for nodeId in replayNodeosArgs:
        cluster.getNode(nodeId).kill(signal.SIGTERM)

    # Start executing test cases here
    Utils.Print("Script Begin .............................")

    # Test regular terminate-at-block
    for nodeId, nodeArgs in regularNodeosArgs.items():
        success = executeTest(
            cluster,
            nodeId,
            nodeArgs,
            testResultMsgs
        )
        if not success:
            break

    # Test terminate-at-block in replay from snapshot and through block logs
    if success:
        for nodeId, nodeArgs in replayNodeosArgs.items():
            success = executeSnapshotBlocklogTest(
                cluster,
                nodeId,
                testResultMsgs,
                nodeArgs,
                replayTermAt[nodeId]
            )
            if not success:
                break

    testSuccessful = success

    Utils.Print("Script End ................................")

finally:
    TestHelper.shutdown(
        cluster,
        walletMgr,
        testSuccessful,
        dumpErrorDetails
    )

    # Print test result
    for msg in testResultMsgs:
        Print(msg)
    if not testSuccessful and len(testResultMsgs) < totalNodes - 1:
        Print("Subsequent tests were not run after failing test scenario.")

exitCode = 0 if testSuccessful else 1
exit(exitCode)
