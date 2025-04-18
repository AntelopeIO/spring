#!/usr/bin/env python3

import time
import signal
import threading
import os
import platform

from TestHarness import Account, Cluster, ReturnType, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# interrupt_read_only_trx_test
#
# Verify an infinite read-only trx can be interrupted by ctrl-c
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

appArgs=AppArgs()
appArgs.add(flag="--read-only-threads", type=int, help="number of read-only threads", default=0)
appArgs.add(flag="--eos-vm-oc-enable", type=str, help="specify eos-vm-oc-enable option", default="auto")
appArgs.add(flag="--wasm-runtime", type=str, help="if wanting eos-vm-oc, must use 'eos-vm-oc-forced'", default="eos-vm-jit")

args=TestHelper.parse_args({"-p","-n","-d","-s","--dump-error-details","-v","--leave-running"
                            ,"--keep-logs","--unshared"}, applicationSpecificArgs=appArgs)

pnodes=args.p
topo=args.s
delay=args.d
total_nodes = pnodes if args.n < pnodes else args.n
# For this test, we need at least 1 non-producer
if total_nodes <= pnodes:
    Print ("non-producing nodes %d must be greater than 0. Force it to %d. producing nodes: %d," % (total_nodes - pnodes, pnodes + 1, pnodes))
    total_nodes = pnodes + 1
debug=args.v
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False
noOC = args.eos_vm_oc_enable == "none"
allOC = args.eos_vm_oc_enable == "all"

# all debuglevel so that "executing ${h} with eos vm oc" is logged
cluster=Cluster(loggingLevel="all", unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)

walletMgr=WalletMgr(True)
EOSIO_ACCT_PRIVATE_DEFAULT_KEY = "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"
EOSIO_ACCT_PUBLIC_DEFAULT_KEY = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"

producerNode = None
apiNode = None
payloadlessAccountName = "payloadless"

def getCodeHash(node, account):
    # Example get code result: code hash: 67d0598c72e2521a1d588161dad20bbe9f8547beb5ce6d14f3abd550ab27d3dc
    cmd = f"get code {account}"
    codeHash = node.processCleosCmd(cmd, cmd, silentErrors=False, returnType=ReturnType.raw)
    if codeHash is None: errorExit(f"Unable to get code {account} from node {node.nodeId}")
    else: codeHash = codeHash.split(' ')[2].strip()
    if Utils.Debug: Utils.Print(f"{account} code hash: {codeHash}")
    return codeHash

def startCluster():
    global total_nodes
    global producerNode
    global apiNode

    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)

    Print ("producing nodes: %d, non-producing nodes: %d, topology: %s, delay between nodes launch(seconds): %d" % (pnodes, total_nodes-pnodes, topo, delay))

    Print("Stand up cluster")
    # set up read-only options for API node
    specificExtraNodeosArgs={}
    # producer nodes will be mapped to 0 through pnodes-1, so the number pnodes is the no-producing API node
    specificExtraNodeosArgs[pnodes]=" --plugin eosio::net_api_plugin"
    specificExtraNodeosArgs[pnodes]+=" --contracts-console "
    specificExtraNodeosArgs[pnodes]+=" --read-only-read-window-time-us "
    specificExtraNodeosArgs[pnodes]+=" 3600000000 " # 1hr to test interrupt of ctrl-c
    specificExtraNodeosArgs[pnodes]+=" --eos-vm-oc-cache-size-mb "
    specificExtraNodeosArgs[pnodes]+=" 1 " # set small so there is churn
    specificExtraNodeosArgs[pnodes]+=" --read-only-threads "
    specificExtraNodeosArgs[pnodes]+=str(args.read_only_threads)
    if args.eos_vm_oc_enable:
        if platform.system() != "Linux":
            Print("OC not run on Linux. Skip the test")
            exit(0) # Do not fail the test
        specificExtraNodeosArgs[pnodes]+=" --eos-vm-oc-enable "
        specificExtraNodeosArgs[pnodes]+=args.eos_vm_oc_enable
    if args.wasm_runtime:
        specificExtraNodeosArgs[pnodes]+=" --wasm-runtime "
        specificExtraNodeosArgs[pnodes]+=args.wasm_runtime
    extraNodeosArgs=" --http-max-response-time-ms 99000000 --disable-subjective-api-billing false "
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo=topo, delay=delay, activateIF=True,
                      specificExtraNodeosArgs=specificExtraNodeosArgs, extraNodeosArgs=extraNodeosArgs ) is False:
        errorExit("Failed to stand up eos cluster.")

    Print ("Wait for Cluster stabilization")
    # wait for cluster to start producing blocks
    if not cluster.waitOnClusterBlockNumSync(3):
        errorExit("Cluster never stabilized")

    producerNode = cluster.getNode()
    apiNode = cluster.nodes[-1]

    eosioCodeHash = getCodeHash(producerNode, "eosio.token")
    # eosio.* should be using oc unless oc tierup disabled
    Utils.Print(f"search: executing {eosioCodeHash} with eos vm oc")
    found = producerNode.findInLog(f"executing {eosioCodeHash} with eos vm oc")
    assert( found or (noOC and not found) )

def deployTestContracts():
    Utils.Print("Create payloadless account and deploy payloadless contract")
    payloadlessAccount = Account(payloadlessAccountName)
    payloadlessAccount.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    payloadlessAccount.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(payloadlessAccount, cluster.eosioAccount, buyRAM=100000)

    payloadlessContractDir="unittests/test-contracts/payloadless"
    payloadlessWasmFile="payloadless.wasm"
    payloadlessAbiFile="payloadless.abi"
    producerNode.publishContract(payloadlessAccount, payloadlessContractDir, payloadlessWasmFile, payloadlessAbiFile, waitForTransBlock=True)

def sendTransaction(account, action, data, auth=[], opts=None):
    trx = {
       "actions": [{
          "account": account,
          "name": action,
          "authorization": auth,
          "data": data
      }]
    }
    return apiNode.pushTransaction(trx, opts, silentErrors=True)

def sendReadOnlyForeverPayloadless():
    try:
        sendTransaction('payloadless', action='doitforever', data={}, auth=[], opts='--read')
    except Exception as e:
        Print("Ignore Exception in sendReadOnlyForeverPayloadless: ", repr(e))

def timeoutTest():
    Print("Timeout Test")

    # Send a forever readonly transaction
    Print("Sending a forever read only transaction")
    trxThread = threading.Thread(target = sendReadOnlyForeverPayloadless)
    trxThread.start()

    # give plenty of time for thread to send read-only trx
    assert producerNode.waitForHeadToAdvance(blocksToAdvance=5)
    assert producerNode.waitForLibToAdvance(), "Producer node stopped advancing LIB"

    Print("Verify node not processing incoming blocks, blocks do not interrupt read-only trx")
    blockNum = apiNode.getHeadBlockNum()
    assert producerNode.waitForLibToAdvance(), "Producer node stopped advancing LIB after forever read-only trx"
    assert blockNum == apiNode.getHeadBlockNum(), "Head still advancing when node should be processing a read-only transaction"

    Print("Verify ctrl-c will interrupt and shutdown node")
    assert apiNode.kill(signal.SIGTERM), "API Node not killed by SIGTERM"

    trxThread.join()

    Print("Verify node will restart")
    assert apiNode.relaunch(), "API Node not restarted after SIGTERM shutdown"


try:
    startCluster()
    deployTestContracts()
    timeoutTest()

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
