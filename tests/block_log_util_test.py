#!/usr/bin/env python3

import time
import os
import signal
import subprocess

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.testUtils import BlockLogAction
from TestHarness.TestHelper import AppArgs
from TestHarness.Node import BlockType

###############################################################
# block_log_util_test
#  Test verifies that the blockLogUtil is still compatible with nodeos
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

def verifyBlockLog(expected_block_num, trimmedBlockLog):
    firstBlockNum = expected_block_num
    for block in trimmedBlockLog:
        assert 'block_num' in block, print("ERROR: spring-util didn't return block output")
        block_num = block['block_num']
        assert block_num == expected_block_num
        expected_block_num += 1
    Print("Block_log contiguous from block number %d to %d" % (firstBlockNum, expected_block_num - 1))


appArgs=AppArgs()
args = TestHelper.parse_args({"--activate-if","--dump-error-details","--keep-logs","-v","--leave-running","--unshared"})
Utils.Debug=args.v
pnodes=2
activateIF=args.activate_if
dumpErrorDetails=args.dump_error_details
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
prodCount=2
walletPort=TestHelper.DEFAULT_WALLET_PORT
totalNodes=pnodes+1

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

WalletdName=Utils.EosWalletName
ClientName="cleos"

try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)

    Print("Stand up cluster")
    if cluster.launch(prodCount=prodCount, onlyBios=False, pnodes=pnodes, totalNodes=totalNodes, totalProducers=pnodes*prodCount, activateIF=activateIF) is False:
        Utils.errorExit("Failed to stand up eos cluster.")

    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    biosNode=cluster.biosNode
    node0=cluster.getNode(0)
    node1=cluster.getNode(1)

    blockNum=100
    Print("Wait till we at least get to block %d" % (blockNum))
    node0.waitForBlock(blockNum, blockType=BlockType.lib)
    info=node0.getInfo(exitOnError=True)
    headBlockNum=info["head_block_num"]
    lib=info["last_irreversible_block_num"]

    Print("Kill the node we want to verify its block log")
    node0.kill(signal.SIGTERM)

    Print("Let's have node1's head advance a few blocks")
    node1.waitForBlock(headBlockNum+4, timeout=18) # timeout should be > 12 in case it is node0's turn (has 2 producers/node)
    infoAfter=node1.getInfo(exitOnError=True)
    headBlockNumAfter=infoAfter["head_block_num"]
    Print(f"headBlockNum = {headBlockNum}, headBlockNumAfter = {headBlockNumAfter}")
    assert headBlockNumAfter > headBlockNum, "head has not advanced on node1"

    Print("Retrieve the whole blocklog for node 0")
    blockLog=cluster.getBlockLog(0)
    bl_nums = [b["block_num"] for b in blockLog]
    bl_consecutive = all(bl_nums[i] - bl_nums[i - 1] == 1 for i in range(1, len(bl_nums)))
    if not bl_consecutive:
        Utils.errorExit(f"BlockLog block numbers should be consecutive, got: {bl_nums}")

    assert headBlockNum in bl_nums, f"Couldn't find block #{headBlockNum} in blocklog:\n{bl_nums}\n"
    assert headBlockNumAfter not in bl_nums, f"Should not find block #{headBlockNumAfter} in blocklog:\n{bl_nums}\n"

    Print("Retrieve the blocklog only for node 0")
    blockLog_only=cluster.getBlockLog(0, blockLogAction=BlockLogAction.return_blocks_only_log)
    assert len(blockLog_only) < len(blockLog), "retrieving blockLog only is expected to be smaller than with fork_db"

    # check that the last block in the blocklog only is lib
    blockLog_lib = blockLog_only[-1]["block_num"]
    assert blockLog_lib == lib or blockLog_lib == lib+1, "last block number of blockLog_only is expected to be lib, or maybe lib+1"

    Print("Retrieve the fork_db only for node 0")
    fork_db_only=cluster.getBlockLog(0, blockLogAction=BlockLogAction.return_blocks_only_fork_db)
    assert len(fork_db_only) < len(blockLog), "retrieving fork_db only is expected to be smaller than with block log"
    assert len(fork_db_only) + len(blockLog_only) == len(blockLog), "size mismatch"
    assert fork_db_only[0]["block_num"] == blockLog_lib+1, "first block number of fork_db_only is expected to be lib+1"
    forkdb_head = fork_db_only[-1]["block_num"]
    assert forkdb_head >= headBlockNum and forkdb_head <= headBlockNum + 3, \
        f"last block number {forkdb_head} of fork_db_only is expected to be headBlockNum {headBlockNum}, or maybe a few after if head advanced after getInfo"

    output=cluster.getBlockLog(0, blockLogAction=BlockLogAction.smoke_test)
    expectedStr="no problems found"
    assert output.find(expectedStr) != -1, "Couldn't find \"%s\" in:\n\"%s\"\n" % (expectedStr, output)

    blockLogDir=Utils.getNodeDataDir(0, "blocks")
    duplicateIndexFileName=os.path.join(blockLogDir, "duplicate.index")
    output=cluster.getBlockLog(0, blockLogAction=BlockLogAction.make_index, outputFile=duplicateIndexFileName)
    assert output is not None, "Couldn't make new index file \"%s\"\n" % (duplicateIndexFileName)

    blockIndexFileName=os.path.join(blockLogDir, "blocks.index")
    blockIndexFile=open(blockIndexFileName,"rb")
    duplicateIndexFile=open(duplicateIndexFileName,"rb")
    blockIndexStr=blockIndexFile.read()
    duplicateIndexStr=duplicateIndexFile.read()
    assert blockIndexStr==duplicateIndexStr, "Generated file \%%s\" didn't match original \"%s\"" % (duplicateIndexFileName, blockIndexFileName)

    try:
        Print("Head block num %d will not be in block log (it will be in reversible DB), so --trim will throw an exception" % (headBlockNum))
        output=cluster.getBlockLog(0, blockLogAction=BlockLogAction.trim, first=0, last=headBlockNum, throwException=True)
        Utils.errorExit("BlockLogUtil --trim should have indicated error for last value set to lib (%d) " +
                        "which should not do anything since only trimming blocklog and not irreversible blocks" % (lib))
    except subprocess.CalledProcessError as ex:
        pass

    beforeEndOfBlockLog=lib-20
    Print("Block num %d will definitely be at least one block behind the most recent entry in block log, so --trim will work" % (beforeEndOfBlockLog))
    output=cluster.getBlockLog(0, blockLogAction=BlockLogAction.trim, first=0, last=beforeEndOfBlockLog, throwException=True)

    Print("Kill the non production node, we want to verify its block log")
    cluster.getNode(2).kill(signal.SIGTERM)

    Print("Trim off block num 1 to remove genesis block from block log.")
    output=cluster.getBlockLog(2, blockLogAction=BlockLogAction.trim, first=2, last=4294967295, throwException=True)

    Print("Smoke test the trimmed block log.")
    output=cluster.getBlockLog(2, blockLogAction=BlockLogAction.smoke_test)

    Print("Analyze block log.")
    trimmedBlockLog=cluster.getBlockLog(2, blockLogAction=BlockLogAction.return_blocks)

    verifyBlockLog(2, trimmedBlockLog)

    # relaunch the node with the truncated block log and ensure it catches back up with the producers
    current_head_block_num = node1.getInfo()["head_block_num"]
    cluster.getNode(2).relaunch()
    assert cluster.getNode(2).waitForBlock(current_head_block_num, timeout=60, reportInterval=15)

    # ensure it continues to advance
    current_head_block_num = node1.getInfo()["head_block_num"]
    assert cluster.getNode(2).waitForBlock(current_head_block_num, timeout=60, reportInterval=15)
    info = cluster.getNode(2).getInfo()
    block = cluster.getNode(2).getBlock(2)
    assert block is not None
    block = cluster.getNode(2).getBlock(1, silentErrors=True)
    assert block is None

    # verify it shuts down cleanly
    cluster.getNode(2).interruptAndVerifyExitStatus()

    firstBlock = info["last_irreversible_block_num"]
    Print("Trim off block num %s." % (firstBlock))
    output=cluster.getBlockLog(2, blockLogAction=BlockLogAction.trim, first=firstBlock, last=4294967295, throwException=True)

    Print("Smoke test the trimmed block log.")
    output=cluster.getBlockLog(2, blockLogAction=BlockLogAction.smoke_test)

    Print("Analyze block log.")
    trimmedBlockLog=cluster.getBlockLog(2, blockLogAction=BlockLogAction.return_blocks)

    verifyBlockLog(firstBlock, trimmedBlockLog)

    # relaunch the node with the truncated block log and ensure it catches back up with the producers
    current_head_block_num = node1.getInfo()["head_block_num"]
    assert current_head_block_num >= info["head_block_num"]
    cluster.getNode(2).relaunch()
    assert cluster.getNode(2).waitForBlock(current_head_block_num, timeout=60, reportInterval=15)

    # ensure it continues to advance
    current_head_block_num = node1.getInfo()["head_block_num"]
    assert cluster.getNode(2).waitForBlock(current_head_block_num, timeout=60, reportInterval=15)
    info = cluster.getNode(2).getInfo()
    block = cluster.getNode(2).getBlock(firstBlock)
    assert block is not None
    block = cluster.getNode(2).getBlock(firstBlock - 1, silentErrors=True)
    assert block is None
    block = cluster.getNode(2).getBlock(1, silentErrors=True)
    assert block is None

    # verify it shuts down cleanly
    cluster.getNode(2).interruptAndVerifyExitStatus()

    testSuccessful=True

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
