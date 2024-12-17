#!/usr/bin/env python3

import json
import signal

from TestHarness import Account, Cluster, Node, ReturnType, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# interrupt_trx_test
#
# Verify an infinite trx in a block is auto recovered when a new
# best head is received.
# 
###############################################################

# Parse command line arguments
args = TestHelper.parse_args({"-v","--dump-error-details","--leave-running","--keep-logs","--unshared"})
Utils.Debug = args.v
dumpErrorDetails=args.dump_error_details
dontKill=args.leave_running
keepLogs=args.keep_logs

EOSIO_ACCT_PRIVATE_DEFAULT_KEY = "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"
EOSIO_ACCT_PUBLIC_DEFAULT_KEY = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"

walletMgr=WalletMgr(True)
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
cluster.setWalletMgr(walletMgr)

testSuccessful = False
try:
    TestHelper.printSystemInfo("BEGIN")
    assert cluster.launch(
        pnodes=2,
        prodCount=2,
        totalProducers=2,
        totalNodes=3,
        loadSystemContract=False,
        activateIF=True,
        extraNodeosArgs="--plugin eosio::test_control_api_plugin")

    prodNode = cluster.getNode(0)
    prodNode2 = cluster.getNode(1)
    validationNode = cluster.getNode(2)

    # load payloadless contract
    Utils.Print("create a new account payloadless from the producer node")
    payloadlessAcc = Account("payloadless")
    payloadlessAcc.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    payloadlessAcc.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    prodNode.createAccount(payloadlessAcc, cluster.eosioAccount)

    contractDir="unittests/test-contracts/payloadless"
    wasmFile="payloadless.wasm"
    abiFile="payloadless.abi"
    Utils.Print("Publish payloadless contract")
    trans = prodNode.publishContract(payloadlessAcc, contractDir, wasmFile, abiFile, waitForTransBlock=True)

    # test normal trx
    contract="payloadless"
    action="doit"
    data="{}"
    opts="--permission payloadless@active"
    trans=prodNode.pushMessage(contract, action, data, opts)
    assert trans and trans[0], "Failed to push doit action"

    # test trx that will be replaced later
    action="doitslow"
    trans=prodNode.pushMessage(contract, action, data, opts)
    assert trans and trans[0], "Failed to push doitslow action"

    # infinite trx, will fail since it will hit trx exec limit
    action="doitforever"
    trans=prodNode.pushMessage(contract, action, data, opts, silentErrors=True)
    assert trans and not trans[0], "push doitforever action did not fail as expected"

    # swap out doitslow action in block with doitforever action
    prodNode.waitForProducer("defproducerb")
    prodNode.waitForProducer("defproducera")

    prodNode.processUrllibRequest("test_control", "swap_action",
                                  {"from":"doitslow", "to":"doitforever",
                                   "trx_priv_key":EOSIO_ACCT_PRIVATE_DEFAULT_KEY,
                                   "blk_priv_key":cluster.defproduceraAccount.activePrivateKey,
                                   "shutdown":"true"})

    # trx that will be swapped out for doitforever
    action="doitslow"
    trans=prodNode.pushMessage(contract, action, data, opts)
    assert trans and trans[0], "Failed to push doitslow action"

    assert prodNode.waitForNodeToExit(5), f"prodNode did not exit after doitforever action and shutdown"
    assert not prodNode.verifyAlive(), f"prodNode did not exit after doitforever action"

    # relaunch and verify auto recovery
    prodNode.relaunch(timeout=365) # large timeout to wait on other producer

    prodNode.waitForProducer("defproducerb")
    prodNode.waitForProducer("defproducera")

    # verify auto recovery without any restart
    prodNode.processUrllibRequest("test_control", "swap_action",
                                  {"from":"doitslow", "to":"doitforever",
                                   "trx_priv_key":EOSIO_ACCT_PRIVATE_DEFAULT_KEY,
                                   "blk_priv_key":cluster.defproduceraAccount.activePrivateKey})

    action="doitslow"
    trans=prodNode.pushMessage(contract, action, data, opts)
    assert trans and trans[0], "Failed to push doitslow action"

    assert prodNode.waitForHeadToAdvance(blocksToAdvance=3), "prodNode not advancing head after doitforever action"
    assert prodNode.waitForLibToAdvance(), "prodNode did not advance lib after doitforever action"
    assert prodNode2.waitForLibToAdvance(), "prodNode2 did not advance lib after doitforever action"

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
