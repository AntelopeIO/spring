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
appArgs.add(flag="--read-only-threads", type=int, help="number of read-only threads", default=2)
appArgs.add(flag="--test-length-seconds", type=int, help="number of seconds to search for a failure", default=10)
appArgs.add(flag="--eos-vm-oc-enable", type=str, help="specify eos-vm-oc-enable option", default="auto")
appArgs.add(flag="--wasm-runtime", type=str, help="if set to eos-vm-oc, must compile with EOSIO_EOS_VM_OC_DEVELOPER", default="eos-vm-jit")

args=TestHelper.parse_args({"-d","-s","--nodes-file","--seed"
                            ,"--activate-if","--dump-error-details","-v","--leave-running"
                            ,"--keep-logs","--unshared"}, applicationSpecificArgs=appArgs)

pnodes=1
topo=args.s
delay=args.d
total_nodes=3
debug=args.v
nodesFile=args.nodes_file
dontLaunch=nodesFile is not None
seed=args.seed
activateIF=args.activate_if
dumpErrorDetails=args.dump_error_details
testLengthSeconds=args.test_length_seconds

Utils.Debug=debug
threadLock=threading.Lock()
allResponsesGood=True
numShortResponses=0
stopThread=False

random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(loggingLevel="all", unshared=args.unshared, keepRunning=True if nodesFile is not None else args.leave_running, keepLogs=args.keep_logs)

walletMgr=WalletMgr(True)
EOSIO_ACCT_PRIVATE_DEFAULT_KEY = "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"
EOSIO_ACCT_PUBLIC_DEFAULT_KEY = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"

producerNode = None
apiNode = None
roTrxNode = None
tightloopAccountName = "tightloop"

def startCluster():
    global total_nodes
    global producerNode
    global apiNode
    global roTrxNode

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
    # last node will be the roTrx node
    roTrxNodeIndex = total_nodes-1
    specificExtraNodeosArgs[roTrxNodeIndex]="--read-only-threads "
    specificExtraNodeosArgs[roTrxNodeIndex]+=str(args.read_only_threads)
    specificExtraNodeosArgs[roTrxNodeIndex]+=" --max-transaction-time "
    specificExtraNodeosArgs[roTrxNodeIndex]+=" 10 "
    if args.eos_vm_oc_enable:
        if platform.system() != "Linux":
            Print("OC not run on Linux. Skip the test")
            exit(True) # Do not fail the test
        specificExtraNodeosArgs[roTrxNodeIndex]+=" --eos-vm-oc-enable "
        specificExtraNodeosArgs[roTrxNodeIndex]+=args.eos_vm_oc_enable
    if args.wasm_runtime:
        specificExtraNodeosArgs[roTrxNodeIndex]+=" --wasm-runtime "
        specificExtraNodeosArgs[roTrxNodeIndex]+=args.wasm_runtime

    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo=topo, delay=delay, activateIF=activateIF, specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    Print ("Wait for Cluster stabilization")
    # wait for cluster to start producing blocks
    if not cluster.waitOnClusterBlockNumSync(3):
        errorExit("Cluster never stabilized")

    producerNode = cluster.getNode()
    roTrxNode = cluster.nodes[-1]
    apiNode = cluster.nodes[-2]


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


def sendROTransaction(account, action, data, auth=[], opts=None):
    trx = {
       "actions": [{
          "account": account,
          "name": action,
          "authorization": auth,
          "data": data
      }]
    }
    return roTrxNode.pushTransaction(trx, opts)

def longROtrxThread():
    Print("start longROtrxThread")

    while stopThread==False:
        sendROTransaction(tightloopAccountName, 'doit', {"count": 50000000}, opts='--read')  #50 million is a good number to always take >10ms

def shortROtrxThread():
    Print("start a shortROtrxThread")

    global numShortResponses
    global allResponsesGood

    while stopThread==False:
        results = sendROTransaction(tightloopAccountName, 'doit', {"count": 250000}, opts='--read')  #250k will hopefully complete within 10ms, even on slower hardware
        with threadLock:
            allResponsesGood &= results[0]
            numShortResponses += 1

def testPassed():
    return allResponsesGood and numShortResponses > 0

try:
    startCluster()
    deployTestContracts()
    # ROtrx won't pump OC's cache so prime OC's cache of this contract with some actions
    for i in range(2):
        trans = apiNode.pushMessage(tightloopAccountName, 'doit', '{"count": 1}', "-p {}@active".format(tightloopAccountName))
        assert(trans[0])
        apiNode.waitForTransactionInBlock(trans[1]['transaction_id'], exitOnError=True)

    # start a background thread that constantly runs a ROtrx that is never expected to complete in max-transaction-time
    thr = threading.Thread(target = longROtrxThread)
    thr.start()

    # start twice the number of configured nodeos RO threads to spam short ROtrx; the 2x is hopefully to keep nodeos RO threads as busy as possible
    shortThreadList = []
    for i in range(args.read_only_threads*2):
        shortThreadList.append(threading.Thread(target = shortROtrxThread))
        shortThreadList[i].start()

    time.sleep(testLengthSeconds)
finally:
    stopThread = True;
    thr.join()
    for sthr in shortThreadList:
        sthr.join()
    TestHelper.shutdown(cluster, walletMgr, testPassed(), dumpErrorDetails)

errorCode = 0 if testPassed() else 1
exit(errorCode)
