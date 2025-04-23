#!/usr/bin/env python3
import os
import shutil
import signal

from http.client import RemoteDisconnected
from TestHarness import Cluster, TestHelper, Utils, WalletMgr

###############################################################
# nodeos_signal_throw_test
#
# Verify throwing in signal handlers is handled correctly by nodeos.
#
# A controller_emit_signal_exception should cause nodeos to gracefully shutdown.
# Any other exception type should be ignored and nodeos continue running.
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-d","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"})
pnodes=1
delay=args.d
debug=args.v
prod_count = 1 # per node prod count
total_nodes=pnodes+2
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, keepRunning=args.leave_running, keepLogs=args.keep_logs)

def verifyExceptionDoesNotShutdown(node, sig, exception):
    if node.isProducer:
        node.waitForProducer("defproducera")
    node.processUrllibRequest("test_control", "throw_on", {"signal":sig, "exception":exception})
    assert node.verifyAlive(), f"Node {node.nodeId} shutdown on {sig} exception {exception}"
    assert node.waitForHeadToAdvance(), f"Node {node.nodeId} did not advance head after {sig} exception {exception}"

def verifyExceptionShutsdown(node, sig, exception):
    if node.isProducer:
        node.waitForProducer("defproducera")
    try:
        node.processUrllibRequest("test_control", "throw_on", {"signal":sig, "exception":exception})
    except RemoteDisconnected as ex:
        pass # ignore as node might shutdown before replying

    assert node.waitForNodeToExit(timeout=10), f"Node {node.nodeId} did not shutdown on {sig} exception {exception}"
    assert not node.verifyAlive(), f"Node {node.nodeId} did not shutdown on {sig} exception {exception}"
    assert node.relaunch(), f"Node {node.nodeId} relaunch failed after {sig} exception {exception}"
    assert node.waitForHeadToAdvance(), f"Node {node.nodeId} did not advance head after relaunch after {sig} exception {exception}"

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    Print("Stand up cluster")
    extraNodeosArgs="--plugin eosio::test_control_api_plugin"
    specificExtraNodeosArgs= {"0": "--enable-stale-production"}
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, totalProducers=pnodes, delay=delay,
                      activateIF=True, extraNodeosArgs=extraNodeosArgs, specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"
    cluster.waitOnClusterSync(blockAdvancing=2)

    node0 = cluster.getNode(0) # producer A
    node1 = cluster.getNode(1) # speculative node for exceptions
    node2 = cluster.getNode(2) # speculative node for trx generator

    Print("Configure and launch txn generators")
    targetTpsPerGenerator = 5
    testTrxGenDurationSec=60*60
    cluster.launchTrxGenerators(contractOwnerAcctName=cluster.eosioAccount.name, acctNamesList=[cluster.defproduceraAccount.name, cluster.eosioAccount.name],
                                acctPrivKeysList=[cluster.defproduceraAccount.activePrivateKey,cluster.eosioAccount.activePrivateKey], nodeId=node2.nodeId,
                                tpsPerGenerator=targetTpsPerGenerator, numGenerators=1, durationSec=testTrxGenDurationSec,
                                waitToComplete=False)

    status = cluster.waitForTrxGeneratorsSpinup(nodeId=node2.nodeId, numGenerators=1)
    assert status is not None and status is not False, "ERROR: Failed to spinup Transaction Generators"


    verifyExceptionDoesNotShutdown(node1, "block_start", "misc")
    verifyExceptionDoesNotShutdown(node1, "accepted_block_header", "misc")
    verifyExceptionDoesNotShutdown(node1, "accepted_block", "misc")
    verifyExceptionDoesNotShutdown(node1, "irreversible_block", "misc")
    verifyExceptionDoesNotShutdown(node1, "applied_transaction", "misc")
    # only applicable to producers:  verifyExceptionDoesNotShutdown(node1, "voted_block", "misc")
    # only applicable to finalizers: verifyExceptionDoesNotShutdown(node1, "aggregated_vote", "misc")

    verifyExceptionDoesNotShutdown(node0, "block_start", "misc")
    verifyExceptionDoesNotShutdown(node0, "accepted_block_header", "misc")
    verifyExceptionDoesNotShutdown(node0, "accepted_block", "misc")
    verifyExceptionDoesNotShutdown(node0, "irreversible_block", "misc")
    verifyExceptionDoesNotShutdown(node0, "applied_transaction", "misc")
    verifyExceptionDoesNotShutdown(node0, "voted_block", "misc")
    verifyExceptionDoesNotShutdown(node0, "aggregated_vote", "misc")

    verifyExceptionShutsdown(node1, "block_start", "controller_emit_signal_exception")
    verifyExceptionShutsdown(node1, "accepted_block_header", "controller_emit_signal_exception")
    verifyExceptionShutsdown(node1, "accepted_block", "controller_emit_signal_exception")
    verifyExceptionShutsdown(node1, "irreversible_block", "controller_emit_signal_exception")
    verifyExceptionShutsdown(node1, "applied_transaction", "controller_emit_signal_exception")
    # only applicable to producers:  verifyExceptionShutsdown(node1, "voted_block", "controller_emit_signal_exception")
    # only applicable to finalizers: verifyExceptionShutsdown(node1, "aggregated_vote", "controller_emit_signal_exception")

    verifyExceptionShutsdown(node0, "block_start", "controller_emit_signal_exception")
    verifyExceptionShutsdown(node0, "accepted_block_header", "controller_emit_signal_exception")
    verifyExceptionShutsdown(node0, "accepted_block", "controller_emit_signal_exception")
    verifyExceptionShutsdown(node0, "irreversible_block", "controller_emit_signal_exception")
    verifyExceptionShutsdown(node0, "applied_transaction", "controller_emit_signal_exception")
    verifyExceptionShutsdown(node0, "voted_block", "controller_emit_signal_exception")
    verifyExceptionShutsdown(node0, "aggregated_vote", "controller_emit_signal_exception")

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
