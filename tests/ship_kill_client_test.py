#!/usr/bin/env python3

import time
import json
import os
import shutil
import signal
import sys

from TestHarness import Account, Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# ship_kill_client_test
#
# Setup a nodeos with SHiP (state_history_plugin).
# Connect a number of clients and then kill the clients and shutdown nodoes.
# nodeos should exit cleanly and not hang or SEGfAULT.
#
###############################################################

Print=Utils.Print

appArgs = AppArgs()
extraArgs = appArgs.add(flag="--num-clients", type=int, help="How many ship_streamers should be started", default=1)
args = TestHelper.parse_args({"--dump-error-details","--keep-logs","-v","--leave-running","--unshared"}, applicationSpecificArgs=appArgs)

Utils.Debug=args.v
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
dumpErrorDetails=args.dump_error_details
walletPort=TestHelper.DEFAULT_WALLET_PORT

# simpler to have two producer nodes then setup different accounts for trx generator
totalProducerNodes=2
totalNonProducerNodes=1
totalNodes=totalProducerNodes+totalNonProducerNodes

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

WalletdName=Utils.EosWalletName
shipTempDir=None

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)
    Print("Stand up cluster")

    shipNodeNum = 2
    specificExtraNodeosArgs={}
    specificExtraNodeosArgs[shipNodeNum]="--plugin eosio::state_history_plugin --trace-history --chain-state-history --finality-data-history --state-history-stride 200 --plugin eosio::net_api_plugin --plugin eosio::producer_api_plugin "

    if cluster.launch(pnodes=totalProducerNodes, loadSystemContract=False,
                      totalNodes=totalNodes, totalProducers=totalProducerNodes, activateIF=True, biosFinalizer=False,
                      specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up cluster.")

    # verify nodes are in sync and advancing
    cluster.waitOnClusterSync(blockAdvancing=5)
    Print("Cluster in Sync")

    prodNode0 = cluster.getNode(0)
    prodNode1 = cluster.getNode(1)
    shipNode = cluster.getNode(shipNodeNum)

    # cluster.waitOnClusterSync(blockAdvancing=3)
    start_block_num = shipNode.getBlockNum()

    #verify nodes are in sync and advancing
    cluster.waitOnClusterSync(blockAdvancing=3)
    Print("Shutdown unneeded bios node")
    cluster.biosNode.kill(signal.SIGTERM)

    Print("Configure and launch txn generators")
    targetTpsPerGenerator = 10
    testTrxGenDurationSec=60*60
    numTrxGenerators=2
    cluster.launchTrxGenerators(contractOwnerAcctName=cluster.eosioAccount.name, acctNamesList=[cluster.defproduceraAccount.name, cluster.defproducerbAccount.name],
                                acctPrivKeysList=[cluster.defproduceraAccount.activePrivateKey,cluster.defproducerbAccount.activePrivateKey], nodeId=prodNode1.nodeId,
                                tpsPerGenerator=targetTpsPerGenerator, numGenerators=numTrxGenerators, durationSec=testTrxGenDurationSec,
                                waitToComplete=False)

    status = cluster.waitForTrxGeneratorsSpinup(nodeId=prodNode1.nodeId, numGenerators=numTrxGenerators)
    assert status is not None and status is not False, "ERROR: Failed to spinup Transaction Generators"

    prodNode1.waitForProducer("defproducera")

    block_range = 100000 # we are going to kill the client, so just make this a huge number
    end_block_num = start_block_num + block_range

    shipClient = "tests/ship_streamer"
    cmd = f"{shipClient} --start-block-num {start_block_num} --end-block-num {end_block_num} --fetch-block --fetch-traces --fetch-deltas --fetch-finality-data"
    if Utils.Debug: Utils.Print(f"cmd: {cmd}")
    clients = []
    files = []
    shipTempDir = os.path.join(Utils.DataDir, "ship")
    os.makedirs(shipTempDir, exist_ok = True)
    shipClientFilePrefix = os.path.join(shipTempDir, "client")

    for i in range(0, args.num_clients):
        outFile = open(f"{shipClientFilePrefix}{i}.out", "w")
        errFile = open(f"{shipClientFilePrefix}{i}.err", "w")
        Print(f"Start client {i}")
        popen=Utils.delayedCheckOutput(cmd, stdout=outFile, stderr=errFile)
        clients.append((popen, cmd))
        files.append((outFile, errFile))
        Print(f"Client {i} started, Ship node head is: {shipNode.getBlockNum()}")


    # allow time for all clients to connect
    shipNode.waitForHeadToAdvance(5)
    shipNode.waitForLibToAdvance()

    Print(f"Kill all {args.num_clients} clients and ship node")
    for index, (popen, _) in zip(range(len(clients)), clients):
        popen.kill()
        if index == len(clients)/2:
            shipNode.kill(signal.SIGTERM)
            assert not shipNode.verifyAlive(), "ship node did not shutdown"

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)
    if shipTempDir is not None:
        if testSuccessful and not args.keep_logs:
            shutil.rmtree(shipTempDir, ignore_errors=True)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
