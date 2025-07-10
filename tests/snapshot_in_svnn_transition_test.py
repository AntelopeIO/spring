#!/usr/bin/env python3

import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# snapshot_in_svnn_transition_test
#
#  Tests snapshot during Savanna transition
#  - Configures 5 producing nodes such that the test does not take too long while has enough
#    transition time
#  - Configures trx_generator to pump transactions into the test node
#  - Take a snapshot right after setfinalizer is called
#  - Wait until LIB advances
#  - Kill the test node
#  - Restart from the snapshot
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

appArgs = AppArgs()
args=TestHelper.parse_args({"-d","-s","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"},
                            applicationSpecificArgs=appArgs)
pnodes=5 # Use 5 such that test does not take too long while has enough transition time
delay=args.d
topo=args.s
debug=args.v
prod_count = 1 # per node prod count
total_nodes=pnodes+1
dumpErrorDetails=args.dump_error_details

snapshotNodeId = 0
irrNodeId=pnodes

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, topology: {topo}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    numTrxGenerators=2
    Print("Stand up cluster")
    # For now do not load system contract as it does not support setfinalizer
    specificExtraNodeosArgs = { irrNodeId: "--read-mode irreversible"}
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, prodCount=prod_count, maximumP2pPerHost=total_nodes+numTrxGenerators, topo=topo, delay=delay, loadSystemContract=False,
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

    nodeSnap=cluster.getNode(snapshotNodeId)
    nodeIrr=cluster.getNode(irrNodeId)

    # Activate Savanna without waiting for activation to be finished so that we can take a
    # snapshot during transition
    success, transId = cluster.activateInstantFinality(biosFinalizer=False, waitForFinalization=False)
    assert success, "Activate instant finality failed"

    # allow time for instant finality activation to be processed
    assert cluster.biosNode.waitForHeadToAdvance(blocksToAdvance=5), "Head should advance after instant finality activate"
    
    # Take snapshots
    def takeSnapshot(node):
       ret = node.createSnapshot()
       assert ret is not None, "snapshot creation failed"
       ret_snaphot_head_block_num = ret["payload"]["head_block_num"]
       Print(f"snapshot head block number {ret_snaphot_head_block_num}")

    Print("Take snapshot on nodeSnap")
    takeSnapshot(nodeSnap)
    Print("Take snapshot on nodeIrr")
    takeSnapshot(nodeIrr)

    assert cluster.biosNode.waitForTransFinalization(transId, timeout=21*12*3), f'Failed to validate transaction {transId} got rolled into a LIB block on server port {cluster.biosNode.port}'
    assert cluster.biosNode.waitForLibToAdvance(), "Lib should advance after instant finality activated"
    assert cluster.biosNode.waitForProducer("defproducera"), "Did not see defproducera"
    assert cluster.biosNode.waitForHeadToAdvance(blocksToAdvance=13), "Head did not advance 13 blocks to next producer"
    assert cluster.biosNode.waitForLibToAdvance(), "Lib stopped advancing on biosNode"
    assert cluster.getNode(snapshotNodeId).waitForLibToAdvance(), "Lib stopped advancing on snapshotNode"
    assert cluster.getNode(irrNodeId).waitForLibToAdvance(), "Lib stopped advancing on irrNode"

    info = cluster.biosNode.getInfo(exitOnError=True)
    assert (info["head_block_num"] - info["last_irreversible_block_num"]) < 9, "Instant finality enabled LIB diff should be small"

    def restartWithSnapshot(node, rmBlocks, updateChainArg=True):
       Print("Shut down node")
       node.kill(signal.SIGTERM)
       Print("Restart node with snapshot")
       node.removeDataDir(rmBlocks=rmBlocks)
       node.rmFromCmd('--p2p-peer-address')
       if updateChainArg: # only add to chainArg once as the node remembers previous chainArgs, avoid two --snapshot passed to node
          chainArg=f"--snapshot {node.getLatestSnapshot()}"
       else:
          chainArg=None
       isRelaunchSuccess = node.relaunch(chainArg=chainArg)
       assert isRelaunchSuccess, "Failed to relaunch node with snapshot"
       assert node.waitForLibToAdvance(timeout=60), "LIB did not advance after restart from snapshot"

    Print("Restart snapshot node with snapshot with blocks")
    restartWithSnapshot(nodeSnap, rmBlocks=False)

    Print("Restart Irreversible node with snapshot with blocks")
    restartWithSnapshot(nodeIrr, rmBlocks=False)

    Print("Restart snapshot node with snapshot without blocks")
    restartWithSnapshot(nodeSnap, rmBlocks=True, updateChainArg=False)

    Print("Restart Irreversible node with snapshot without blocks")
    restartWithSnapshot(nodeIrr, rmBlocks=True, updateChainArg=False)

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
