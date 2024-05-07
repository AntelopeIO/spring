#!/usr/bin/env python3

import json
import os
import re
import shutil
import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr

###############################################################
# ship_reqs_across_savanna
# 
# This test verifies SHiP get_blocks_result_v1 works across Legacy and Savanna boundary.
#   1. Start a producer Node and a SHiP node in Legacy mode
#   2. Transition to Savanna
#   3. Start a SHiP client, requesting blocks between block number 1 (pre Savanna)
#      and a block whose block number greater than Savanna Genesis block (post Savanna)
#   4. Verify `finality_data` field in every block before Savanna Genesis block is NULL,
#      and `finality_data` field in every block after Savanna Genesis block contains a value.
# 
###############################################################

Print=Utils.Print

args = TestHelper.parse_args({"--dump-error-details","--keep-logs","-v","--leave-running","--unshared"})

Utils.Debug=args.v
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
dumpErrorDetails=args.dump_error_details
walletPort=TestHelper.DEFAULT_WALLET_PORT

totalProducerNodes=1
totalNonProducerNodes=1
totalNodes=totalProducerNodes+totalNonProducerNodes

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

shipTempDir=None

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)
    Print("Stand up cluster")

    shipNodeNum = 1
    specificExtraNodeosArgs={}
    specificExtraNodeosArgs[shipNodeNum]="--plugin eosio::state_history_plugin --trace-history --chain-state-history --state-history-stride 200 --plugin eosio::net_api_plugin --plugin eosio::producer_api_plugin --finality-data-history"

    if cluster.launch(topo="mesh", pnodes=totalProducerNodes, totalNodes=totalNodes,
                      activateIF=True,
                      specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up cluster.")

    # Verify nodes are in sync and advancing
    cluster.waitOnClusterSync(blockAdvancing=5)
    Print("Cluster in Sync")

    Print("Shutdown unneeded bios node")
    cluster.biosNode.kill(signal.SIGTERM)

    shipNode = cluster.getNode(shipNodeNum)
    # Block with start_block_num is before Savanna and block with end_block_num is after Savanna
    start_block_num = 1
    end_block_num = shipNode.getBlockNum()

    # Start a SHiP client and request blocks between start_block_num and end_block_num
    shipClient = "tests/ship_streamer"
    cmd = f"{shipClient} --start-block-num {start_block_num} --end-block-num {end_block_num} --fetch-block --fetch-traces --fetch-deltas --fetch-finality-data"
    if Utils.Debug: Utils.Print(f"cmd: {cmd}")
    shipTempDir = os.path.join(Utils.DataDir, "ship")
    os.makedirs(shipTempDir, exist_ok = True)
    shipClientFilePrefix = os.path.join(shipTempDir, "client")
    clientOutFileName = f"{shipClientFilePrefix}.out"
    clientOutFile = open(clientOutFileName, "w")
    clientErrFile = open(f"{shipClientFilePrefix}.err", "w")
    Print(f"Start client")
    popen=Utils.delayedCheckOutput(cmd, stdout=clientOutFile, stderr=clientErrFile)
    Print(f"Client started, Ship node head is: {shipNode.getBlockNum()}")

    Print("Wait for SHiP client to finish")
    popen.wait()
    Print("SHiP client stopped")
    clientOutFile.close()
    clientErrFile.close()

    Print("Shutdown SHiP node")
    shipNode.kill(signal.SIGTERM)

    # Find the Savanna Genesis Block number
    svnnGensisBlockNum = 0
    shipStderrFileName=Utils.getNodeDataDir(shipNodeNum, "stderr.txt")
    with open(shipStderrFileName, 'r') as f:
        line = f.readline()
        while line:
            match =  re.search(r'Transitioning to savanna, IF Genesis Block (\d+)', line)
            if match:
                svnnGensisBlockNum = int(match.group(1))
                break
            line = f.readline()
    Print(f"Savanna Genesis Block number: {svnnGensisBlockNum}")

    # Make sure start_block_num is indeed pre Savanna and end_block_num is post Savanna
    assert svnnGensisBlockNum > start_block_num, f'svnnGensisBlockNum {svnnGensisBlockNum} must be greater than start_block_num {start_block_num}'
    assert svnnGensisBlockNum < end_block_num, f'svnnGensisBlockNum {svnnGensisBlockNum} must be less than end_block_num {end_block_num}'

    Print("Verify ship_client output is well formed")
    blocks_result_v1 = None
    # Verify SHiP client receives well formed results
    with open(clientOutFileName, 'r') as f:
        lines = f.readlines()
        try:
            blocks_result_v1 = json.loads(" ".join(lines))
        except json.decoder.JSONDecodeError as er:
            Utils.errorExit(f"ship_client output was malformed. Exception: {er}")

    # Verify `finality_data` field in every block before Savanna Genesis block is Null,
    # and `finality_data` field in every block after Savanna Genesis block has a value.
    Print("Verify finality_data")
    for result in blocks_result_v1:
        res = result["get_blocks_result_v1"]
        block_num = int(res["this_block"]["block_num"])
        finality_data = res["finality_data"]

        if block_num < svnnGensisBlockNum:
            assert finality_data is None, f"finality_data is not Null for block {block_num} before Savanna Genesis Block {svnnGensisBlockNum}"
        else:
            assert finality_data is not None, f"finality_data is Null for block {block_num} after Savanna Genesis Block {svnnGensisBlockNum}"

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)
    if shipTempDir is not None:
        if testSuccessful and not args.keep_logs:
            shutil.rmtree(shipTempDir, ignore_errors=True)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
