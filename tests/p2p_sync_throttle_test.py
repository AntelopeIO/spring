#!/usr/bin/env python3

import math
import re
import signal
import sys
import time
import urllib

from TestHarness import Cluster, TestHelper, Utils, WalletMgr, CORE_SYMBOL, createAccountKeys, ReturnType
from TestHarness.TestHelper import AppArgs

###############################################################
# p2p_sync_throttle_test
#
# Test throttling of a peer during block syncing.
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

appArgs = AppArgs()
appArgs.add(flag='--plugin',action='append',type=str,help='Run nodes with additional plugins')
appArgs.add(flag='--connection-cleanup-period',type=int,help='Interval in whole seconds to run the connection reaper and metric collection')

args=TestHelper.parse_args({"-d","--keep-logs","--activate-if"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--unshared"},
                            applicationSpecificArgs=appArgs)
pnodes=1
delay=args.d
debug=args.v
prod_count = 2
total_nodes=5
activateIF=args.activate_if
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    Print("Stand up cluster")
    # Custom topology:
    #    prodNode <-> nonProdNode
    #                             <-> throttlingNode <-> throttledNode  (20KB/s)
    #                                                <-> unThrottledNode (1TB/s)
    #
    # Compare the sync time of throttledNode and unThrottledNode
    if cluster.launch(pnodes=pnodes, unstartedNodes=3, totalNodes=total_nodes, prodCount=prod_count,
                      extraNodeosArgs="--sync-fetch-span 5",
                      topo='./tests/p2p_sync_throttle_test_shape.json', delay=delay, activateIF=activateIF) is False:
        errorExit("Failed to stand up eos cluster.")

    prodNode = cluster.getNode(0)
    nonProdNode = cluster.getNode(1)

    accounts=createAccountKeys(2)
    if accounts is None:
        Utils.errorExit("FAILURE - create keys")

    accounts[0].name="tester111111"
    accounts[1].name="tester222222"

    account1PrivKey = accounts[0].activePrivateKey
    account2PrivKey = accounts[1].activePrivateKey

    testWalletName="test"

    Print("Creating wallet \"%s\"." % (testWalletName))
    testWallet=walletMgr.create(testWalletName, [cluster.eosioAccount,accounts[0],accounts[1]])

    # create accounts via eosio as otherwise a bid is needed
    for account in accounts:
        Print("Create new account %s via %s" % (account.name, cluster.eosioAccount.name))
        trans=nonProdNode.createInitializeAccount(account, cluster.eosioAccount, stakedDeposit=0, waitForTransBlock=True, stakeNet=1000, stakeCPU=1000, buyRAM=1000, exitOnError=True)
        transferAmount="100000000.0000 {0}".format(CORE_SYMBOL)
        Print("Transfer funds %s from account %s to %s" % (transferAmount, cluster.eosioAccount.name, account.name))
        nonProdNode.transferFunds(cluster.eosioAccount, account, transferAmount, "test transfer", waitForTransBlock=True)
        trans=nonProdNode.delegatebw(account, 20000000.0000, 20000000.0000, waitForTransBlock=True, exitOnError=True)

    beginLargeBlocksHeadBlock = nonProdNode.getHeadBlockNum()

    Print("Configure and launch txn generators")
    targetTpsPerGenerator = 500
    testTrxGenDurationSec=90
    trxGeneratorCnt=1
    cluster.launchTrxGenerators(contractOwnerAcctName=cluster.eosioAccount.name, acctNamesList=[accounts[0].name,accounts[1].name],
                                acctPrivKeysList=[account1PrivKey,account2PrivKey], nodeId=prodNode.nodeId, tpsPerGenerator=targetTpsPerGenerator,
                                numGenerators=trxGeneratorCnt, durationSec=testTrxGenDurationSec, waitToComplete=True)

    endLargeBlocksHeadBlock = nonProdNode.getHeadBlockNum()

    throttlingNode = cluster.unstartedNodes[0]
    i = throttlingNode.cmd.index('--p2p-listen-endpoint')
    throttleListenAddr = throttlingNode.cmd[i+1]
    # Using 20 Kilobytes per second to allow syncing of ~250 transaction blocks at ~175 bytes per transaction
    # (250*175=43750 per block or 87500 per second)
    # resulting from the trx generators in a reasonable amount of time
    throttlingNode.cmd[i+1] = throttlingNode.cmd[i+1] + ':20KB/s'
    throttleListenIP, throttleListenPort = throttleListenAddr.split(':')
    throttlingNode.cmd.append('--p2p-listen-endpoint')
    unThrottleListenAddr = f'{throttleListenIP}:{int(throttleListenPort)+100}'
    throttlingNode.cmd.append(f'{unThrottleListenAddr}:1TB/s')

    cluster.biosNode.kill(signal.SIGTERM)

    Print("Launch throttling node")
    cluster.launchUnstarted(1)
    assert throttlingNode.verifyAlive(), "throttling node did not launch"

    # Throttling node was offline during block generation and once online receives blocks as fast as possible
    assert throttlingNode.waitForBlock(endLargeBlocksHeadBlock), f'wait for block {endLargeBlocksHeadBlock}  on throttled node timed out'

    throttledNode = cluster.unstartedNodes[0]
    throttledNode.cmd.append('--p2p-peer-address')
    throttledNode.cmd.append(throttleListenAddr)
    unThrottledNode = cluster.unstartedNodes[1]
    unThrottledNode.cmd.append('--p2p-peer-address')
    unThrottledNode.cmd.append(unThrottleListenAddr)

    Print("Launch throttled and un-throttled nodes")
    clusterStart = time.time()
    cluster.launchUnstarted(2)

    throttledNode = cluster.getNode(3)
    unThrottledNode = cluster.getNode(4)
    assert throttledNode.verifyAlive(), "throttled node did not launch"
    assert unThrottledNode.verifyAlive(), "un-throttled node did not launch"

    assert unThrottledNode.waitForBlock(endLargeBlocksHeadBlock), f'wait for block {endLargeBlocksHeadBlock}  on un-throttled node timed out'
    endUnThrottledSync = time.time()

    assert throttledNode.waitForBlock(endLargeBlocksHeadBlock, timeout=endLargeBlocksHeadBlock*2), f'Wait for block {endLargeBlocksHeadBlock} on throttled node timed out'
    endThrottledSync = time.time()

    throttledElapsed = endThrottledSync - clusterStart
    unThrottledElapsed = endUnThrottledSync - clusterStart
    Print(f'Un-throttled sync time: {unThrottledElapsed} seconds')
    Print(f'Throttled sync time: {throttledElapsed} seconds')

    assert throttledElapsed > 2 * unThrottledElapsed, f'Throttled node did not sync slower {throttledElapsed} <= {2 * unThrottledElapsed}'

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
