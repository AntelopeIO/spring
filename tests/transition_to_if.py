#!/usr/bin/env python3

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs
from TestHarness.Node import BlockType

###############################################################
# transition_to_if
#
# Transition to instant-finality with multiple producers (at least 4).
#
###############################################################


Print=Utils.Print
errorExit=Utils.errorExit

appArgs = AppArgs()
args=TestHelper.parse_args({"-p","-d","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"},
                            applicationSpecificArgs=appArgs)
pnodes=args.p if args.p > 4 else 4
delay=args.d
debug=args.v
prod_count = 1 # per node prod count
total_nodes=pnodes+1
irreversibleNodeId=pnodes
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, keepRunning=args.leave_running, keepLogs=args.keep_logs)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    numTrxGenerators=2
    Print("Stand up cluster")
    # For now do not load system contract as it does not support setfinalizer
    specificExtraNodeosArgs = { irreversibleNodeId: "--read-mode irreversible"}
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, prodCount=prod_count, maximumP2pPerHost=total_nodes+numTrxGenerators, delay=delay, loadSystemContract=False,
                      activateIF=False, specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"

    Print("Configure and launch txn generators")
    targetTpsPerGenerator = 10
    testTrxGenDurationSec=60*60
    cluster.launchTrxGenerators(contractOwnerAcctName=cluster.eosioAccount.name, acctNamesList=[cluster.defproduceraAccount.name, cluster.defproducerbAccount.name],
                                acctPrivKeysList=[cluster.defproduceraAccount.activePrivateKey,cluster.defproducerbAccount.activePrivateKey], nodeId=cluster.getNode(0).nodeId,
                                tpsPerGenerator=targetTpsPerGenerator, numGenerators=numTrxGenerators, durationSec=testTrxGenDurationSec,
                                waitToComplete=False)

    status = cluster.waitForTrxGeneratorsSpinup(nodeId=cluster.getNode(0).nodeId, numGenerators=numTrxGenerators)
    assert status is not None and status is not False, "ERROR: Failed to spinup Transaction Generators"

    Print("Start transition to Savanna")
    success, transId = cluster.activateInstantFinality(biosFinalizer=False, waitForFinalization=False)
    assert success, "Activate instant finality failed"

    cluster.biosNode.waitForHeadToAdvance()

    Print("Verify calling setfinalizers again does no harm")
    success, ignoredId = cluster.activateInstantFinality(biosFinalizer=True, waitForFinalization=False)

    Print("Wait for LIB of setfinalizers")
    if not cluster.biosNode.waitForTransFinalization(transId, timeout=21 * 12 * 3):
        Utils.Print("ERROR: Failed to validate setfinalizer transaction %s got rolled into a LIB block" % (transId))

    assert cluster.biosNode.waitForLibToAdvance(), "Lib should advance after instant finality activated"
    assert cluster.biosNode.waitForProducer("defproducera"), "Did not see defproducera"
    assert cluster.biosNode.waitForHeadToAdvance(blocksToAdvance=13), "Head did not advance 13 blocks to next producer"
    assert cluster.biosNode.waitForLibToAdvance(), "Lib stopped advancing on biosNode"
    assert cluster.getNode(1).waitForLibToAdvance(), "Lib stopped advancing on Node 1"
    assert cluster.getNode(irreversibleNodeId).waitForLibToAdvance(), f"Lib stopped advancing on Node {irreversibleNodeId}, irreversible node"

    info = cluster.biosNode.getInfo(exitOnError=True)
    assert (info["head_block_num"] - info["last_irreversible_block_num"]) < 9, "Instant finality enabled LIB diff should be small"

    # launcher setup node_00 (defproducera,e,i,m,q,u), node_01 (defproducerb,f,j,n,r),
    #                node_02 (defproducerc,g,k,o,s),   node_03 (defproducerd,h,l,p,t)
    # launcher setprods with first producer of each node (defproducera,b,c,d)
    assert cluster.biosNode.waitForProducer("defproducerd"), "defproducerd did not produce"

    Print("Set prods")
    # should take effect in first block of defproducerb slot (so defproducerc)
    assert cluster.setProds(["defproducere", "defproducerf", "defproducerg", "defproducerh"]), "setprods failed"
    setProdsBlockNum = cluster.biosNode.getBlockNum()
    assert cluster.biosNode.waitForBlock(setProdsBlockNum+12+12+1), "Block of new producers not reached"
    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] == "defproducerf", "setprods should have taken effect"
    assert cluster.getNode(4).waitForBlock(setProdsBlockNum + 12 + 12 + 1), "Block of new producers not reached on irreversible node"

    Print("Set finalizers")
    nodes = [cluster.getNode(0), cluster.getNode(1), cluster.getNode(2)]
    transId = cluster.setFinalizers(nodes)
    assert transId is not None, "setfinalizers failed"
    if not cluster.biosNode.waitForTransFinalization(transId):
        Utils.Print("ERROR: setfinalizer transaction %s not rolled into a LIB block" % (transId))
    currentHead = cluster.biosNode.getHeadBlockNum()
    assert cluster.biosNode.waitForBlock(currentHead, blockType=BlockType.lib), "LIB did not advance for setfinalizers to activate"
    assert cluster.biosNode.waitForLibToAdvance(), "LIB did not advance after setfinalizers"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
