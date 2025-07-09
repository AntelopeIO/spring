#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import time
import unittest
import os
import signal

from TestHarness import Cluster, Node, TestHelper, Utils, WalletMgr, CORE_SYMBOL, createAccountKeys

testSuccessful = False

class TraceApiPluginTest(unittest.TestCase):
    cluster=Cluster(defproduceraPrvtKey=None)
    walletMgr=WalletMgr(True)
    accounts = []
    cluster.setWalletMgr(walletMgr)

    # start keosd and nodeos
    def startEnv(self) :
        account_names = ["alice", "bob", "charlie"]
        abs_path = os.path.abspath(os.getcwd() + '/unittests/contracts/eosio.token/eosio.token.abi')
        extraNodeosArgs = " --verbose-http-errors --trace-slice-stride 10000 --trace-rpc-abi eosio.token=" + abs_path
        extraNodeosArgs+=" --production-pause-vote-timeout-ms 0"
        self.cluster.launch(totalNodes=2, activateIF=True, extraNodeosArgs=extraNodeosArgs)
        self.walletMgr.launch()
        testWalletName="testwallet"
        testWallet=self.walletMgr.create(testWalletName, [self.cluster.eosioAccount, self.cluster.defproduceraAccount])
        self.cluster.validateAccounts(None)
        self.accounts=createAccountKeys(len(account_names))
        node = self.cluster.getNode(1)
        for idx in range(len(account_names)):
            self.accounts[idx].name =  account_names[idx]
            self.walletMgr.importKey(self.accounts[idx], testWallet)
        for account in self.accounts:
            node.createInitializeAccount(account, self.cluster.eosioAccount, buyRAM=1000000, stakedDeposit=5000000, waitForTransBlock=True if account == self.accounts[-1] else False, exitOnError=True)

    def get_block(self, params: str, node: Node) -> json:
        resource = "trace_api"
        command = "get_block"
        payload = {"block_num" : params}
        return node.processUrllibRequest(resource, command, payload)

    def test_TraceApi(self) :
        node = self.cluster.getNode(0)
        for account in self.accounts:
            self.assertIsNotNone(node.verifyAccount(account))

        expectedAmount = Node.currencyIntToStr(5000000, CORE_SYMBOL)
        account_balances = []
        for account in self.accounts:
            amount = node.getAccountEosBalanceStr(account.name)
            self.assertEqual(amount, expectedAmount)
            account_balances.append(amount)

        xferAmount = Node.currencyIntToStr(123456, CORE_SYMBOL)
        trans = node.transferFunds(self.accounts[0], self.accounts[1], xferAmount, "test transfer a->b")
        transId = Node.getTransId(trans)
        blockNum = Node.getTransBlockNum(trans)

        self.assertEqual(node.getAccountEosBalanceStr(self.accounts[0].name), Utils.deduceAmount(expectedAmount, xferAmount))
        self.assertEqual(node.getAccountEosBalanceStr(self.accounts[1].name), Utils.addAmount(expectedAmount, xferAmount))
        node.waitForBlock(blockNum)

        # verify trans via node api before calling trace_api RPC
        blockFromNode = node.getBlock(blockNum)
        self.assertIn("transactions", blockFromNode)
        isTrxInBlockFromNode = False
        for trx in blockFromNode["transactions"]:
            self.assertIn("trx", trx)
            self.assertIn("id", trx["trx"])
            if (trx["trx"]["id"] == transId) :
                isTrxInBlockFromNode = True
                break
        self.assertTrue(isTrxInBlockFromNode)

        # verify trans via trace_api by calling get_block RPC
        blockFromTraceApi = self.get_block(blockNum, node)
        self.assertIn("transactions", blockFromTraceApi["payload"])
        isTrxInBlockFromTraceApi = False
        for trx in blockFromTraceApi["payload"]["transactions"]:
            self.assertIn("id", trx)
            if (trx["id"] == transId) :
                isTrxInBlockFromTraceApi = True
                self.assertIn('actions', trx)
                actions = trx['actions']
                for act in actions:
                    self.assertIn('params', act)
                    prms = act['params']
                    self.assertIn('from', prms)
                    self.assertIn('to', prms)
                    self.assertIn('quantity', prms)
                    self.assertIn('memo', prms)
                break
        self.assertTrue(isTrxInBlockFromTraceApi)

        # use trace_api to get transId
        node.getTransaction(transId, silentErrors=False, exitOnError=True)

        # relaunch with no time allocated for http response & abi-serializer
        node.kill(signal.SIGTERM)
        isRelaunchSuccess = node.relaunch(timeout=10, chainArg="--enable-stale-production", addSwapFlags={"--http-max-response-time-ms": "0", "--abi-serializer-max-time-ms": "0"})
        self.assertTrue(isRelaunchSuccess)

        # Verify get block_trace still works even with no time for http-max-response-time-ms and no time for bi-serializer-max-time-ms
        cmdDesc="get block_trace"
        cmd=" --print-response %s %d" % (cmdDesc, blockNum)
        cmd="%s %s %s" % (Utils.EosClientPath, node.eosClientArgs(), cmd)
        result=Utils.runCmdReturnStr(cmd, ignoreError=True)
        Utils.Print(f"{cmdDesc} returned: {result}")
        self.assertIn("test transfer a->b", result)

        Utils.Print("Verify get transaction_trace works for trx after loading from a snapshot")
        Utils.Print("Create snapshot (node 0)")
        ret = node.createSnapshot()
        assert ret is not None, "Snapshot creation failed"
        ret_head_block_num = ret["payload"]["head_block_num"]
        Utils.Print(f"Snapshot head block number {ret_head_block_num}")
        node.kill(signal.SIGTERM)
        assert not node.verifyAlive(), "Node did not shutdown"
        node.removeState()
        node.removeReversibleBlks()
        node.removeTracesDir()

        isRelaunchSuccess = node.relaunch(chainArg="--snapshot {}".format(node.getLatestSnapshot()), addSwapFlags={"--trace-slice-stride": "2"})
        assert isRelaunchSuccess, f"node0 relaunch from snapshot failed"
        assert node.waitForHeadToAdvance(), "Node0 did not advance HEAD after relaunch"

        # Removed trace dir above, should not find the transId
        Utils.Print("Verify can't find transaction trace because trace dir removed")
        assert not node.getTransaction(transId, silentErrors=True, exitOnError=False)

        # Transfer after restart, should be able to find this one
        Utils.Print("New transfer transactions")
        xferAmount = Node.currencyIntToStr(1, CORE_SYMBOL)
        transIds = []
        for i in range(0, 3): # stride is 2
            trans = node.transferFunds(self.accounts[0], self.accounts[1], xferAmount, "test transfer a->b", force=True)
            node.waitForHeadToAdvance()
            transId = Node.getTransId(trans)
            transIds.append(transId)
        Utils.Print("Find new transactions created after snapshot")
        for transId in transIds:
            assert node.getTransaction(transId, silentErrors=False, exitOnError=True)

        global testSuccessful
        testSuccessful = True
    @classmethod
    def setUpClass(self):
        self.startEnv(self)

    @classmethod
    def tearDownClass(self):
        TraceApiPluginTest.cluster.testFailed = not testSuccessful
        TraceApiPluginTest.cluster.shutdown()

if __name__ == "__main__":
    unittest.main() # main() exits with non-zero (1) if any assert* fails. no need to call exit() explicitly again

