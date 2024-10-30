#!/usr/bin/env python3

import json
import signal

from TestHarness import Account, Cluster, Node, ReturnType, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# interrupt_trx_test
#
# Test applying a block with an infinite trx and verify SIGTERM kill
# interrupts the transaction and aborts the block.
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
        pnodes=1,
        prodCount=1,
        totalProducers=1,
        totalNodes=2,
        loadSystemContract=False,
        activateIF=True,
        extraNodeosArgs="--plugin eosio::test_control_api_plugin")

    prodNode = cluster.getNode(0)
    validationNode = cluster.getNode(1)

    # Create a transaction to create an account
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

    contract="payloadless"
    action="doit"
    data="{}"
    opts="--permission payloadless@active"
    trans=prodNode.pushMessage(contract, action, data, opts)
    assert trans and trans[0], "Failed to push doit action"

    action="doitslow"
    trans=prodNode.pushMessage(contract, action, data, opts)
    assert trans and trans[0], "Failed to push doitslow action"

    action="doitforever"
    trans=prodNode.pushMessage(contract, action, data, opts, silentErrors=True)
    assert trans and not trans[0], "push doitforever action did not fail as expected"

    prodNode.processUrllibRequest("test_control", "swap_action", {"from": "doitslow", "to": "doitforever"})

    action="doitslow"
    trans=prodNode.pushMessage(contract, action, data, opts)
    assert trans and trans[0], "Failed to push doitslow action"

    prodNode.waitForProducer("defproducera")

    # Needs https://github.com/AntelopeIO/spring/issues/876
    # prodNode.processUrllibRequest("test_control", "swap_action",
    #                               {"from":"doitslow", "to":"doitforever",
    #                                "trx_priv_key":EOSIO_ACCT_PRIVATE_DEFAULT_KEY,
    #                                "blk_priv_key":cluster.defproduceraAccount.activePrivateKey})
    #
    # assert not prodNode.waitForHeadToAdvance(3), f"prodNode did advance head after doitforever action"
    #
    # prodNode.popenProc.send_signal(signal.SIGTERM)
    #
    # assert not prodNode.verifyAlive(), "prodNode did not exit from SIGTERM"

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
