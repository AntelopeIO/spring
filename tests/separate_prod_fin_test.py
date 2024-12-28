#!/usr/bin/env python3

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# separate_prod_fin_test
#
# Test producer nodes and finalizer nodes which are separate. Configure 2 producer nodes and
# 3 non-producer nodes; each of them has a finalizer key. Since threshold is 4,
# if LIB advances, it implies at least 2 non-producer finalizer participates in
# the finalization process.
# Also verifies finalizer can run with vote-threads=0
#
###############################################################


Print=Utils.Print
errorExit=Utils.errorExit

appArgs = AppArgs()
args=TestHelper.parse_args({"-d","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"},
                            applicationSpecificArgs=appArgs)
pnodes=2 # producer node
delay=args.d
debug=args.v
total_nodes=pnodes+3 # 3 non-producer nodes
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
    # For now do not load system contract as it does not support setfinalizer
    # separate_prod_fin_test_shape.json defines 2 producer nodes each has 1
    # producer and 3 non-producer nodes
    specificExtraNodeosArgs={}
    specificExtraNodeosArgs[total_nodes-1]="--vote-threads 0 "
    specificExtraNodeosArgs[total_nodes-2]="--vote-threads 0 "
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, totalProducers=pnodes,
                      topo="./tests/separate_prod_fin_test_shape.json", delay=delay,
                      activateIF=True, signatureProviderForNonProducer=True) is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"

    cluster.biosNode.waitForHeadToAdvance()

    Print("Wait for LIB advancing")
    assert cluster.biosNode.waitForLibToAdvance(), "Lib should advance after instant finality activated"
    assert cluster.biosNode.waitForProducer("defproducera"), "Did not see defproducera"
    assert cluster.biosNode.waitForHeadToAdvance(blocksToAdvance=13), "Head did not advance 13 blocks to next producer"
    assert cluster.biosNode.waitForLibToAdvance(), "Lib stopped advancing on biosNode"
    assert cluster.getNode(1).waitForLibToAdvance(), "Lib stopped advancing on Node 1"

    info = cluster.biosNode.getInfo(exitOnError=True)
    assert (info["head_block_num"] - info["last_irreversible_block_num"]) < 9, "Instant finality enabled LIB diff should be small"

    # Double check that's indeed the case in qc_extension
    i = 0
    while i < 3: # slow ci/cd might not get all votes in time, try a few blocks
        # LIB has advanced, which indicate at least 2 of non-producer finalizers have voted.
        block_num = cluster.getNode(1).getIrreversibleBlockNum()
        block = cluster.getNode(1).getBlock(block_num)
        if "qc_extension" in block:
            qc_ext = block["qc_extension"]
            Print(f'{qc_ext}')
            # "11111" is the representation of a bitset showing which finalizers have voted (we have five total finalizers)
            if qc_ext["qc"]["active_policy_sig"]["strong_votes"] == "11111":
                break
        cluster.getNode(1).waitForLibToAdvance()
        i += 1
    assert i < 3, "Not all finalizers voted"


    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
