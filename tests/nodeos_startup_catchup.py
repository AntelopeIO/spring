#!/usr/bin/env python3

import time
import signal
import decimal
import math
import re

from TestHarness import Cluster, Node, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType
from TestHarness.TestHelper import AppArgs

###############################################################
# nodeos_startup_catchup
#
#  Test configures a producing node and <--txn-plugins count> non-producing nodes.
#  Configures trx_generator(s) and starts generating transactions and sending them
#  to the producing node.
#  1) After 10 seconds a new node is started.
#  2) the node is allowed to catch up to the producing node
#  3) that node is killed
#  4) restart the node
#  5) the node is allowed to catch up to the producing node
#  3) Repeat steps 2-5, <--catchup-count - 1> more times
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

appArgs=AppArgs()
extraArgs = appArgs.add(flag="--catchup-count", type=int, help="How many catchup-nodes to launch", default=16)
extraArgs = appArgs.add(flag="--txn-gen-nodes", type=int, help="How many transaction generator nodes", default=2)
args = TestHelper.parse_args({"--dump-error-details","--keep-logs","-v","--leave-running",
                              "--activate-if","-p","--wallet-port","--unshared"}, applicationSpecificArgs=appArgs)
Utils.Debug=args.v
pnodes=args.p if args.p > 0 else 1
startedNonProdNodes = args.txn_gen_nodes if args.txn_gen_nodes >= 2 else 2
trxGeneratorCnt=startedNonProdNodes
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
dumpErrorDetails=args.dump_error_details
prodCount=2
walletPort=args.wallet_port
catchupCount=args.catchup_count if args.catchup_count > 0 else 1
totalNodes=startedNonProdNodes+pnodes+catchupCount
activateIF=args.activate_if

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

WalletdName=Utils.EosWalletName
ClientName="cleos"
trxGenLauncher=None

try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)

    Print("Stand up cluster")
    specificExtraNodeosArgs = {}
    specificExtraNodeosArgs[pnodes+1] = f' --sync-fetch-span 1 '
    specificExtraNodeosArgs[pnodes+2] = f' --sync-fetch-span 5 '
    specificExtraNodeosArgs[pnodes+3] = f' --sync-fetch-span 21 '
    specificExtraNodeosArgs[pnodes+4] = f' --sync-fetch-span 89 '
    specificExtraNodeosArgs[pnodes+5] = f' --sync-fetch-span 177 '
    specificExtraNodeosArgs[pnodes+6] = f' --sync-fetch-span 1597 '
    specificExtraNodeosArgs[pnodes+7] = f' --sync-fetch-span 2500 '
    specificExtraNodeosArgs[pnodes+8] = f' --sync-fetch-span 6765 '
    specificExtraNodeosArgs[pnodes+9] = f' --sync-fetch-span 28657 '
    specificExtraNodeosArgs[pnodes+10] = f' ' # default
    specificExtraNodeosArgs[pnodes+11] = f' --sync-fetch-span 1 --read-mode irreversible '
    specificExtraNodeosArgs[pnodes+12] = f' --sync-fetch-span 5 --read-mode irreversible '
    specificExtraNodeosArgs[pnodes+13] = f' --sync-fetch-span 89 --read-mode irreversible '
    specificExtraNodeosArgs[pnodes+14] = f' --sync-fetch-span 150 --read-mode irreversible '
    specificExtraNodeosArgs[pnodes+15] = f' --sync-fetch-span 2500 --read-mode irreversible '
    if cluster.launch(prodCount=prodCount, specificExtraNodeosArgs=specificExtraNodeosArgs, activateIF=activateIF, onlyBios=False,
                      pnodes=pnodes, totalNodes=totalNodes, totalProducers=pnodes*prodCount, unstartedNodes=catchupCount,
                      loadSystemContract=True, maximumP2pPerHost=totalNodes+trxGeneratorCnt) is False:
        Utils.errorExit("Failed to stand up eos cluster.")

    Print("Create test wallet")
    wallet = walletMgr.create('txntestwallet')
    cluster.populateWallet(2, wallet)

    Print("Create test accounts for transactions.")
    cluster.createAccounts(cluster.eosioAccount, stakedDeposit=0)

    account1Name = cluster.accounts[0].name
    account2Name = cluster.accounts[1].name

    account1PrivKey = cluster.accounts[0].activePrivateKey
    account2PrivKey = cluster.accounts[1].activePrivateKey

    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts([cluster.accounts[0], cluster.accounts[1]])

    def lib(node):
        return node.getBlockNum(BlockType.lib)

    def head(node):
        return node.getBlockNum(BlockType.head)

    def waitForBlock(node, blockNum, blockType=BlockType.head, timeout=None, reportInterval=20):
        if not node.waitForBlock(blockNum, timeout=timeout, blockType=blockType, reportInterval=reportInterval):
            info=node.getInfo()
            headBlockNum=info["head_block_num"]
            libBlockNum=info["last_irreversible_block_num"]
            Utils.errorExit("Failed to get to %s block number %d. Last had head block number %d and lib %d" % (blockType, blockNum, headBlockNum, libBlockNum))

    def waitForNodeStarted(node):
        sleepTime=0
        while sleepTime < 10 and node.getInfo(silentErrors=True) is None:
            time.sleep(1)
            sleepTime+=1

    node0=cluster.getNode(0)

    Print("Wait for account creation to be irreversible")
    blockNum=head(node0)
    waitForBlock(node0, blockNum, blockType=BlockType.lib)

    Print("Configure and launch txn generators")
    targetTpsPerGenerator = 10
    testTrxGenDurationSec=60*60
    cluster.launchTrxGenerators(contractOwnerAcctName=cluster.eosioAccount.name, acctNamesList=[account1Name, account2Name],
                                acctPrivKeysList=[account1PrivKey,account2PrivKey], nodeId=node0.nodeId,
                                tpsPerGenerator=targetTpsPerGenerator, numGenerators=trxGeneratorCnt, durationSec=testTrxGenDurationSec,
                                waitToComplete=False)

    status = cluster.waitForTrxGeneratorsSpinup(nodeId=node0.nodeId, numGenerators=trxGeneratorCnt)
    assert status is not None, "ERROR: Failed to spinup Transaction Generators"

    blockNum=head(node0)
    timePerBlock=500
    transactionsPerBlock=targetTpsPerGenerator*trxGeneratorCnt*timePerBlock/1000
    steadyStateWait=20
    startBlockNum=blockNum+steadyStateWait
    numBlocks=400
    endBlockNum=startBlockNum+numBlocks
    waitForBlock(node0, endBlockNum, timeout=numBlocks)
    steadyStateWindowTrxs=0
    steadyStateAvg=0
    steadyStateWindowBlks=0
    for bNum in range(startBlockNum, endBlockNum):
        steadyStateWindowBlks=steadyStateWindowBlks+1
        block=node0.getBlock(bNum)
        steadyStateWindowTrxs+=len(block["transactions"])

    steadyStateAvg=steadyStateWindowTrxs / steadyStateWindowBlks

    Print("Validate transactions are generating")
    minReqPctLeeway=0.85
    minRequiredTransactions=minReqPctLeeway*transactionsPerBlock
    assert steadyStateAvg>=minRequiredTransactions, \
        (f"Expected to at least receive {minRequiredTransactions} transactions per block, "
         f"but only getting {steadyStateAvg} for blocks {startBlockNum} - {endBlockNum}")

    Print("Cycle through catchup scenarios")
    twoRounds=21*2*12
    twoRoundsTimeout=(twoRounds/2 + 10)  #2 rounds in seconds + some leeway
    for catchup_num in range(0, catchupCount):
        Print("Start catchup node")
        cluster.launchUnstarted()
        lastLibNum=lib(node0)
        # verify producer lib is still advancing
        waitForBlock(node0, lastLibNum+1, timeout=twoRoundsTimeout, blockType=BlockType.lib)

        catchupNode=cluster.getNodes()[-1]
        catchupNodeNum=cluster.getNodes().index(catchupNode)
        waitForNodeStarted(catchupNode)
        lastCatchupLibNum=lib(catchupNode)

        Print("Verify catchup node %s's LIB is advancing" % (catchupNodeNum))
        # verify lib is advancing (before we wait for it to have to catchup with producer)
        waitForBlock(catchupNode, lastCatchupLibNum+1, timeout=twoRoundsTimeout, blockType=BlockType.lib)

        Print("Verify catchup node is advancing to producer")
        numBlocksToCatchup=(lastLibNum-lastCatchupLibNum-1)+twoRounds
        waitForBlock(catchupNode, lastLibNum, timeout=twoRoundsTimeout, blockType=BlockType.lib)

        catchupHead=head(catchupNode)
        Print("Shutdown catchup node and validate exit code")
        catchupNode.interruptAndVerifyExitStatus(60)

        Print(f"Wait for producer to advance lib past head of catchup {catchupHead}")
        # catchupHead+5 to allow for advancement of head during shutdown of catchupNode
        waitForBlock(node0, catchupHead+5, timeout=twoRoundsTimeout*2, blockType=BlockType.lib)

        Print("Restart catchup node")
        catchupNode.relaunch(skipGenesis=False)
        waitForNodeStarted(catchupNode)
        lastCatchupLibNum=lib(catchupNode)

        Print("Verify catchup node is advancing")
        # verify catchup node is advancing to producer
        waitForBlock(catchupNode, lastLibNum, timeout=twoRoundsTimeout, blockType=BlockType.lib)

        Print("Verify producer is still advancing LIB")
        lastLibNum=lib(node0)
        # verify producer lib is still advancing
        node0.waitForBlock(lastLibNum+1, timeout=twoRoundsTimeout, blockType=BlockType.lib)

        Print("Verify catchup node is advancing to producer")
        # verify catchup node is advancing to producer
        waitForBlock(catchupNode, lastLibNum, timeout=(numBlocksToCatchup/2 + 60), blockType=BlockType.lib)
        catchupNode.interruptAndVerifyExitStatus(60)

        Print("Verify catchup without a block log")
        addSwapFlags = {"--block-log-retain-blocks": "0", "--delete-all": ""}
        catchupNode.relaunch(skipGenesis=False, addSwapFlags=addSwapFlags)
        waitForNodeStarted(catchupNode)
        lastCatchupLibNum=lib(catchupNode)

        Print("Verify catchup node is advancing without block log")
        # verify catchup node is advancing to producer
        waitForBlock(catchupNode, lastLibNum+1, timeout=twoRoundsTimeout, blockType=BlockType.lib)
        catchupNode.interruptAndVerifyExitStatus(60)
        catchupNode.popenProc=None

        # Verify not syncing ahead of sync-fetch-span
        sync_fetch_span = 1000 # default
        irreversible = False
        if catchupNode.nodeId in specificExtraNodeosArgs:
            m = re.search(r"sync-fetch-span (\d+)", specificExtraNodeosArgs[catchupNode.nodeId])
            if m is not None:
                sync_fetch_span = int(m.group(1))
            irreversible = re.search(r"irreversible", specificExtraNodeosArgs[catchupNode.nodeId]) is not None
        Print(f"Verify request span for sync-fetch-span {sync_fetch_span} of {catchupNode.data_dir}")
        lines = catchupNode.linesInLog("requesting range")
        for line in lines:
            m = re.search(r"requesting range (\d+) to (\d+), fhead (\d+), lib (\d+)", line)
            if m is not None:
                startBlockNum=int(m.group(1))
                endBlockNum=int(m.group(2))
                fhead=int(m.group(3))
                libNum=int(m.group(4))
                if endBlockNum-startBlockNum > sync_fetch_span:
                    errorExit(f"Requested range exceeds sync-fetch-span {sync_fetch_span}: {line}")
                if irreversible:
                    # for now just use a larger tolerance, later when the logs include calculated lib this can be more precise
                    # See https://github.com/AntelopeIO/spring/issues/806
                    if activateIF:
                        if endBlockNum > fhead and fhead > libNum and endBlockNum - fhead > (sync_fetch_span*10):
                            errorExit(f"Requested range too far head of fork head {fhead} in irreversible mode, sync-fetch-span {sync_fetch_span}: {line}")
                    else:
                        if endBlockNum > fhead and fhead > libNum and endBlockNum - fhead > (sync_fetch_span*2+360):
                            errorExit(f"Requested range too far head of fork head {fhead} in irreversible mode, sync-fetch-span {sync_fetch_span}: {line}")

                else:
                    if endBlockNum > fhead and fhead > libNum and endBlockNum - fhead > (sync_fetch_span*2-1):
                        errorExit(f"Requested range too far head of fork head {fhead} sync-fetch-span {sync_fetch_span}: {line}")

        # See https://github.com/AntelopeIO/spring/issues/81 for fix to reduce the number of expected unlinkable blocks
        # Test verifies LIB is advancing, check to see that not too many unlinkable block exceptions are generated
        # while syncing up to head.
        if not catchupNode.verifyUnlinkableBlocksExpected(sync_fetch_span):
            errorExit(f"unlinkable blocks are not expected") # details already logged in verifyUnlinkableBlocksExpected

    testSuccessful=True

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
