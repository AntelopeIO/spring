#!/usr/bin/env python3

import random
import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# terminate-scenarios-test
#
# Tests terminate scenarios for nodeos.  Uses "-c" flag to indicate "replay" (--replay-blockchain), "resync"
# (--delete-all-blocks), "hardReplay"(--hard-replay-blockchain), and "none" to indicate what kind of restart flag should
# be used. This is one of the only test that actually verify that nodeos terminates with a good exit status.
#
###############################################################


Print=Utils.Print
errorExit=Utils.errorExit

appArgs=AppArgs()
args=TestHelper.parse_args({"-d","-c","--kill-sig","--keep-logs"
                            ,"--activate-if","--dump-error-details","-v","--leave-running"
                            ,"--terminate-at-block","--unshared"}, applicationSpecificArgs=appArgs)
pnodes=1
topo="./tests/terminate_scenarios_test_shape.json"
delay=args.d
chainSyncStrategyStr=args.c
debug=args.v
total_nodes = pnodes+1
killSignalStr=args.kill_sig
activateIF=args.activate_if
dumpErrorDetails=args.dump_error_details
terminate=args.terminate_at_block

seed=1
Utils.Debug=debug
testSuccessful=False

random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)

try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)
    cluster.setChainStrategy(chainSyncStrategyStr)

    Print ("producing nodes: %d, topology: %s, delay between nodes launch(seconds): %d, chain sync strategy: %s" % (
           pnodes, topo, delay, chainSyncStrategyStr))

    Print("Stand up cluster")
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, totalProducers=pnodes, topo=topo, delay=delay, activateIF=activateIF) is False:
        errorExit("Failed to stand up eos cluster.")

    Print ("Wait for Cluster stabilization")
    # wait for cluster to start producing blocks
    if not cluster.waitOnClusterBlockNumSync(3):
        errorExit("Cluster never stabilized")

    # make sure enough blocks produced to verify truncate works on restart
    cluster.getNode(0).waitForBlock(terminate+5)

    Print(f"Kill signal {killSignalStr} cluster node instance 0.")
    killSignal = signal.SIGKILL
    if killSignalStr == Utils.SigTermTag:
        killSignal = signal.SIGTERM
    if not cluster.getNode(0).kill(killSignal):
        errorExit("Failed to kill Eos instances")
    assert not cluster.getNode(0).verifyAlive()
    Print("nodeos instances 0 killed.")

    Print ("Relaunch dead cluster node instance.")
    nodeArg = "--terminate-at-block %d" % terminate if terminate > 0 else ""
    if nodeArg != "":
        if chainSyncStrategyStr == "hardReplay":
            nodeArg += " --truncate-at-block %d" % terminate
    nodeArg += " --enable-stale-production "
    if cluster.relaunchEosInstances(nodeArgs=nodeArg, waitForTerm=(terminate > 0)) is False:
        errorExit("Failed to relaunch Eos instance")
    Print("nodeos instance relaunched.")

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
