#!/usr/bin/env python3

import signal
import json
import time

from TestHarness import Account, Cluster, TestHelper, Utils, WalletMgr, CORE_SYMBOL
from TestHarness.TestHelper import AppArgs

###############################################################
# p2p_no_blocks_test
#
# Test p2p-listen-address trx only option
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

appArgs = AppArgs()
appArgs.add(flag='--plugin',action='append',type=str,help='Run nodes with additional plugins')
appArgs.add(flag='--connection-cleanup-period',type=int,help='Interval in whole seconds to run the connection reaper and metric collection')

args=TestHelper.parse_args({"-d","--keep-logs"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--unshared"},
                            applicationSpecificArgs=appArgs)
pnodes=1
delay=args.d
debug=args.v
prod_count = 2
total_nodes=4
activateIF=True
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
    #    prodNode00 <-> nonProdNode01
    #                        -> noBlocks02 :9902 (p2p-listen-address with trx only) (speculative mode)
    #                        -> noBlocks03 :9903 (p2p-listen-address with trx only)
    #
    # 01-nonProdNode connects to 02 & 03, but 02 & 03 do not connect to 01 so they will not receive any blocks
    # 02 & 03 are connected to the bios node to get blocks until bios node is killed.
    #
    specificExtraNodeosArgs = {}
    # nonProdNode01 will connect normally but will not send blocks because 02 & 03 have specified :trx only
    specificExtraNodeosArgs[1] = f'--p2p-peer-address localhost:9902 --p2p-peer-address localhost:9903 '
    # add a trx only listen endpoint to noBlocks02 & noBlocks03
    specificExtraNodeosArgs[2] =  f'--p2p-peer-address localhost:9776 --read-mode speculative ' # connect to bios
    specificExtraNodeosArgs[2] += f'--p2p-listen-endpoint localhost:9878 --p2p-server-address localhost:9878 '
    specificExtraNodeosArgs[2] += f'--p2p-listen-endpoint localhost:9902:trx --p2p-server-address localhost:9902:trx '
    specificExtraNodeosArgs[3] =  f'--p2p-peer-address localhost:9776 ' # connect to bios
    specificExtraNodeosArgs[3] += f'--p2p-listen-endpoint localhost:9879 --p2p-server-address localhost:9879 '
    specificExtraNodeosArgs[3] += f'--p2p-listen-endpoint localhost:9903:trx --p2p-server-address localhost:9903:trx '
    if cluster.launch(pnodes=pnodes, unstartedNodes=2, totalNodes=total_nodes, prodCount=prod_count,
                      extraNodeosArgs="--connection-cleanup-period 3", specificExtraNodeosArgs=specificExtraNodeosArgs,
                      topo='./tests/p2p_no_blocks_test_shape.json', delay=delay, activateIF=activateIF, biosFinalizer=False) is False:
        errorExit("Failed to stand up eos cluster.")

    prodNode00 = cluster.getNode(0)
    nonProdNode01 = cluster.getNode(1)

    noBlocks02 = cluster.unstartedNodes[0]
    noBlocks03 = cluster.unstartedNodes[1]

    Print("Launch no block nodes 02 & 03")
    cluster.launchUnstarted(2)

    assert noBlocks02.verifyAlive(), "node 02 did not launch"
    assert noBlocks03.verifyAlive(), "node 03 did not launch"

    headBlockNum = nonProdNode01.getHeadBlockNum()

    Print("Sync from bios")
    assert noBlocks02.waitForBlock(headBlockNum), "node02 did not sync from bios"
    assert noBlocks03.waitForBlock(headBlockNum), "node03 did not sync from bios"

    # create transfer transaction now so it has a valid TAPOS
    eosioBalanceBefore = nonProdNode01.getAccountEosBalance("eosio")
    defprodueraBalanceBefore = nonProdNode01.getAccountEosBalance("defproducera")
    transferAmount="50.0000 {0}".format(CORE_SYMBOL)
    trx=nonProdNode01.transferFunds(cluster.eosioAccount, cluster.defproduceraAccount, transferAmount, dontSend=True)

    Print("Sync past transfer reference block")
    # Make sure node2 and node3 have trx reference block before killing bios node otherwise the trx will fail
    # because the reference block is not available.
    headBlockNum = nonProdNode01.getHeadBlockNum()
    assert noBlocks02.waitForBlock(headBlockNum), "node02 did not get block before bios shutdown"
    assert noBlocks03.waitForBlock(headBlockNum), "node03 did not get block before bios shutdown"

    Print("Killing bios node")
    cluster.biosNode.kill(signal.SIGTERM)

    # blocks could be received but not processed, so give a bit of delay for blocks to be processed
    time.sleep(1)

    Print("Verify head no longer advancing after bios killed")
    assert not noBlocks02.waitForHeadToAdvance(), "head advanced on node02 unexpectedly"
    assert not noBlocks03.waitForHeadToAdvance(), "head advanced on node03 unexpectedly"

    Print("Send transfer trx")
    cmdDesc = "push transaction --skip-sign"
    cmd     = "%s '%s'" % (cmdDesc, json.dumps(trx))
    trans = nonProdNode01.processCleosCmd(cmd, cmdDesc, silentErrors=False, exitOnError=True)

    # can't wait for transaction in block, so just sleep
    time.sleep(0.5)

    eosioBalanceNode02 = noBlocks02.getAccountEosBalance("eosio")
    defprodueraBalanceNode02 = noBlocks02.getAccountEosBalance("defproducera")
    eosioBalanceNode03 = noBlocks03.getAccountEosBalance("eosio")
    defprodueraBalanceNode03 = noBlocks03.getAccountEosBalance("defproducera")

    assert eosioBalanceBefore - 500000 == eosioBalanceNode02, f"{eosioBalanceBefore - 500000} != {eosioBalanceNode02}"
    assert defprodueraBalanceBefore + 500000 == defprodueraBalanceNode02, f"{defprodueraBalanceBefore + 500000} != {defprodueraBalanceNode02}"

    assert eosioBalanceBefore == eosioBalanceNode03, f"{eosioBalanceBefore} != {eosioBalanceNode03}"
    assert defprodueraBalanceBefore == defprodueraBalanceNode03, f"{defprodueraBalanceBefore} != {defprodueraBalanceNode03}"

    cluster.biosNode.relaunch()

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
