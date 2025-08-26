#!/usr/bin/env python3
import signal
import platform

from TestHarness import Account, Cluster, TestHelper, Utils, WalletMgr

###############################################################
# sync_call_restart
#
# Tests restart of a node with sync_call protocol feature already activated.
#
# We had a bug that resources required by sync calls (in particular for OC) were
# not allocated on restart if sync_call protocol feature had already been activated
# https://github.com/AntelopeIO/spring/issues/1847
#
# This test activates sync_call protocol feature, does a snapshot, kills the node,
# restarts the node, pushes a transaction containing 2-levels of nested sync calls,
# and verifies node not crashed.
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"--keep-logs","--dump-error-details","-v","--leave-running","--unshared"})
pnodes=1
debug=args.v
total_nodes=pnodes
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, keepRunning=args.leave_running, keepLogs=args.keep_logs)

try:
    if platform.system() != "Linux":
        Print("OC not run on Linux. Skip the test")
        exit(True) # Do not fail the test

    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    # Enable OC so that the test can execute paths invloved OC reources
    specificExtraNodeosArgs={0: " --eos-vm-oc-enable all "}

    Print("Stand up cluster")
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, activateIF=True, specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        errorExit("Failed to stand up cluster.")

    cluster.waitOnClusterSync(blockAdvancing=5)

    node=cluster.getNode(0)

    def deployContract(account_name, contract_name):
        acct=Account(account_name)
        acct.ownerPublicKey="PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63"
        acct.activePublicKey="PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63"
        cluster.createAccountAndVerify(acct, cluster.eosioAccount)
        node.publishContract(acct, f"unittests/test-contracts/{contract_name}", f"{contract_name}.wasm", f"{contract_name}.abi", waitForTransBlock=True)

    Print("Create accounts and publish contracts")
    deployContract("caller", "sync_caller");
    deployContract("callee", "sync_callee");
    deployContract("callee1", "sync_callee1");

    Print("Create snapshot");
    ret=node.createSnapshot()
    assert ret is not None, "Snapshot creation failed"

    Print("Stopping snapshot");
    node.kill(signal.SIGTERM)
    assert not node.verifyAlive(), "Node did not shutdown"

    node.removeState()
    node.removeReversibleBlks()

    Print("Restart from snapshot");
    isRelaunchSuccess=node.relaunch(chainArg=f"--snapshot {node.getLatestSnapshot()}")
    assert isRelaunchSuccess, "node relaunch from snapshot failed"

    Print("Push a transaction containing 2-levels of a nested sync call");
    trx={"actions": [{"account": "caller", "name": "nestedcalls", "authorization": [{"actor": "caller","permission": "active"}], "data": {}}]}
    results=node.pushTransaction(trx)
    assert results[0], "pushTransaction failed"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode=0 if testSuccessful else 1
exit(exitCode)
