#!/usr/bin/env python3

import random
import time
import signal
import threading
import os
import platform
import traceback

from TestHarness import Account, Cluster, ReturnType, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# read_only_trx_overlap_timeout tests that a ROtrx which hits the deadline timer on one thread doesn't
#  disturb ROtrx on other threads
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

appArgs=AppArgs()
appArgs.add(flag="--test-length-seconds", type=int, help="number of seconds to search for a failure", default=10)
appArgs.add(flag="--eos-vm-oc-enable", type=str, help="specify eos-vm-oc-enable option", default="auto")
appArgs.add(flag="--wasm-runtime", type=str, help="if set to eos-vm-oc, must compile with EOSIO_EOS_VM_OC_DEVELOPER", default="eos-vm-jit")

args=TestHelper.parse_args({"-d","-s","--nodes-file","--seed"
                            ,"--activate-if","--dump-error-details","-v","--leave-running"
                            ,"--keep-logs","--unshared"}, applicationSpecificArgs=appArgs)

pnodes=1
topo=args.s
delay=args.d
total_nodes=2
debug=args.v
nodesFile=args.nodes_file
dontLaunch=nodesFile is not None
seed=args.seed
activateIF=args.activate_if
dumpErrorDetails=args.dump_error_details
testLengthSeconds=args.test_length_seconds

Utils.Debug=debug
testSuccessful=False
stopThread=False

random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(loggingLevel="all", unshared=args.unshared, keepRunning=True if nodesFile is not None else args.leave_running, keepLogs=args.keep_logs)

walletMgr=WalletMgr(True)
EOSIO_ACCT_PRIVATE_DEFAULT_KEY = "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"
EOSIO_ACCT_PUBLIC_DEFAULT_KEY = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"

producerNode = None
apiNode = None
tightloopAccountName = "tightloop"

def startCluster():
    global total_nodes
    global producerNode
    global apiNode

    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)

    if dontLaunch: # run test against remote cluster
        jsonStr=None
        with open(nodesFile, "r") as f:
            jsonStr=f.read()
        if not cluster.initializeNodesFromJson(jsonStr):
            errorExit("Failed to initialize nodes from Json string.")
        total_nodes=len(cluster.getNodes())

        print("Stand up walletd")
        if walletMgr.launch() is False:
            errorExit("Failed to stand up keosd.")

    Print("producing nodes: %d, non-producing nodes: %d, topology: %s, delay between nodes launch(seconds): %d" % (pnodes, total_nodes-pnodes, topo, delay))

    Print("Stand up cluster")
    # set up read-only options for API node
    specificExtraNodeosArgs={}
    # producer nodes will be mapped to 0 through pnodes-1, so the number pnodes is the no-producing API node
    specificExtraNodeosArgs[pnodes]="--read-only-threads "
    specificExtraNodeosArgs[pnodes]+=" 2 "
    specificExtraNodeosArgs[pnodes]+=" --max-transaction-time "
    specificExtraNodeosArgs[pnodes]+=" 10 "
    if args.eos_vm_oc_enable:
        if platform.system() != "Linux":
            Print("OC not run on Linux. Skip the test")
            exit(True) # Do not fail the test
        specificExtraNodeosArgs[pnodes]+=" --eos-vm-oc-enable "
        specificExtraNodeosArgs[pnodes]+=args.eos_vm_oc_enable
    if args.wasm_runtime:
        specificExtraNodeosArgs[pnodes]+=" --wasm-runtime "
        specificExtraNodeosArgs[pnodes]+=args.wasm_runtime

    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo=topo, delay=delay, activateIF=activateIF, specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    Print ("Wait for Cluster stabilization")
    # wait for cluster to start producing blocks
    if not cluster.waitOnClusterBlockNumSync(3):
        errorExit("Cluster never stabilized")

    producerNode = cluster.getNode()
    apiNode = cluster.nodes[-1]


def deployTestContracts():
    Utils.Print("create test accounts")
    tightloopAccount = Account(tightloopAccountName)
    tightloopAccount.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    tightloopAccount.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(tightloopAccount, cluster.eosioAccount, buyRAM=1000000)

    tightloopContractDir="unittests/test-contracts/tightloop"
    tightloopWasmFile="tightloop.wasm"
    tightloopAbiFile="tightloop.abi"
    producerNode.publishContract(tightloopAccount, tightloopContractDir, tightloopWasmFile, tightloopAbiFile, waitForTransBlock=True)


def sendTransaction(account, action, data, auth=[], opts=None):
    trx = {
       "actions": [{
          "account": account,
          "name": action,
          "authorization": auth,
          "data": data
      }]
    }
    return apiNode.pushTransaction(trx, opts)

def longROtrxThread():
    Print("start longROtrxThread")

    while stopThread==False:
        sendTransaction(tightloopAccountName, 'doit', {"count": 50000000}, opts='--read')  #50 million is a good number to always take >10ms

try:
    startCluster()
    deployTestContracts()

    # start a background thread that constantly runs a ROtrx that is never expected to complete in max-transaction-time
    thr = threading.Thread(target = longROtrxThread)
    thr.start()

    endTime = time.time() + testLengthSeconds
    # and then run some other ROtrx that should complete successfully
    while time.time() < endTime:
        results = sendTransaction(tightloopAccountName, 'doit', {"count": 1000000}, opts='--read')  #1 million is a good number to always take <10ms
        assert(results[0])

    testSuccessful = True
finally:
    stopThread = True;
    thr.join()
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
