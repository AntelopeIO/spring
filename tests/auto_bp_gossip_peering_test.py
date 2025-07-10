#!/usr/bin/env python3

import copy
import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr, CORE_SYMBOL, createAccountKeys

###############################################################
# auto_bp_gossip_peering_test
#
# This test sets up a cluster with 21 producers nodeos, each nodeos is configured with only one producer and only
# connects to the bios node. Moreover, each producer nodeos is also configured with a p2p-bp-gossip-endpoint so that
# each one can automatically establish p2p connections to other bps. Test verifies connections are established when
# producer schedule is active.
#
# Test then changes the producer schedule and verifies that connections change appropriately.
# Also verifies manual connections are maintained and that non-producers disconnect from producers when they are no
# longer in the schedule.
#
###############################################################

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
producerNodes = 21
producerCountInEachNode = 1
totalNodes = producerNodes

# Parse command line arguments
args = TestHelper.parse_args({
    "-v",
    "--dump-error-details",
    "--leave-running",
    "--keep-logs",
    "--unshared"
})

Utils.Debug = args.v
dumpErrorDetails = args.dump_error_details
keepLogs = args.keep_logs

# Setup cluster and its wallet manager
walletMgr = WalletMgr(True)
cluster = Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
cluster.setWalletMgr(walletMgr)

def getHostName(nodeId):
    port = cluster.p2pBasePort + nodeId
    if producer_name == 'defproducerf':
        hostname = 'ext-ip0:9999'
    else:
        hostname = "localhost:" + str(port)
    return hostname

peer_names = {}
for nodeId in range(0, producerNodes):
    producer_name = "defproducer" + chr(ord('a') + nodeId)
    port = cluster.p2pBasePort + nodeId
    hostname = getHostName(nodeId)
    peer_names[hostname] = producer_name

auto_bp_peer_arg = f" --p2p-auto-bp-peer defproducera,localhost:{cluster.p2pBasePort}"

peer_names["localhost:9776"] = "bios"

testSuccessful = False
try:
    accounts=createAccountKeys(21)
    if accounts is None:
        Utils.errorExit("FAILURE - create keys")
    voteAccounts=createAccountKeys(5)
    if voteAccounts is None:
        Utils.errorExit("FAILURE - create keys")
    voteAccounts[0].name="tester111111"
    voteAccounts[1].name="tester222222"
    voteAccounts[2].name="tester333333"
    voteAccounts[3].name="tester444444"
    voteAccounts[4].name="tester555555"

    if walletMgr.launch() is False:
        errorExit("Failed to stand up keosd.")

    specificNodeosArgs = {}
    for nodeId in range(0, producerNodes):
        specificNodeosArgs[nodeId] = auto_bp_peer_arg
        producer_name = "defproducer" + chr(ord('a') + nodeId)
        specificNodeosArgs[nodeId] += (f" --signature-provider {accounts[nodeId].activePublicKey}=KEY:{accounts[nodeId].activePrivateKey}")

    specificNodeosArgs[5] = specificNodeosArgs[5] + ' --p2p-server-address ext-ip0:9999'

    # restarting all producers can trigger production pause, so disable
    # net_api_plugin for /v1/net/connections
    extraNodeosArgs = " --production-pause-vote-timeout-ms 0 --plugin eosio::net_api_plugin "

    TestHelper.printSystemInfo("BEGIN")
    cluster.launch(
        prodCount=producerCountInEachNode,
        totalNodes=totalNodes,
        pnodes=producerNodes,
        totalProducers=producerNodes,
        activateIF=True,
        topo="./tests/auto_bp_peering_test_shape.json",
        extraNodeosArgs=extraNodeosArgs,
        specificExtraNodeosArgs=specificNodeosArgs,
    )

    testWalletName="test"
    Print("Creating wallet \"%s\"" % (testWalletName))
    walletAccounts=copy.deepcopy(cluster.defProducerAccounts)
    testWallet = walletMgr.create(testWalletName, walletAccounts.values())
    walletMgr.importKeys(voteAccounts, testWallet)
    all_acc = accounts + list( cluster.defProducerAccounts.values() )
    for account in all_acc:
        Print("Importing keys for account %s into wallet %s." % (account.name, testWallet.name))
        if not walletMgr.importKey(account, testWallet):
            errorExit("Failed to import key for account %s" % (account.name))

    for i in range(0, producerNodes):
        node=cluster.getNode(i)
        node.producers=Cluster.parseProducers(i)
        for prod in node.producers:
            trans=cluster.biosNode.regproducer(cluster.defProducerAccounts[prod], "http::/mysite.com", 0,
                                               waitForTransBlock=False, silentErrors=False)
    Print("Setup vote accounts so they can vote")
    # create accounts via eosio as otherwise a bid is needed
    for account in voteAccounts:
        Print("Create new account %s via %s" % (account.name, cluster.eosioAccount.name))
        trans=cluster.biosNode.createInitializeAccount(account, cluster.eosioAccount, stakedDeposit=0, waitForTransBlock=False, stakeNet=1000, stakeCPU=1000, buyRAM=1000, exitOnError=True)
        cluster.biosNode.waitForTransactionInBlock(trans['transaction_id'])
        transferAmount="100000000.0000 {0}".format(CORE_SYMBOL)
        Print("Transfer funds %s from account %s to %s" % (transferAmount, cluster.eosioAccount.name, account.name))
        trans=cluster.biosNode.transferFunds(cluster.eosioAccount, account, transferAmount, "test transfer", waitForTransBlock=False)
        cluster.biosNode.waitForTransactionInBlock(trans['transaction_id'])
        trans=cluster.biosNode.delegatebw(account, 20000000.0000, 20000000.0000, waitForTransBlock=False, exitOnError=False)

    Print("regpeerkey for all the producers")
    for nodeId in range(0, producerNodes):
        producer_name = "defproducer" + chr(ord('a') + nodeId)
        a = accounts[nodeId]
        node = cluster.getNode(nodeId)

        success, trans = cluster.biosNode.pushMessage('eosio', 'regpeerkey', f'{{"proposer_finalizer_name":"{producer_name}","key":"{a.activePublicKey}"}}', f'-p {producer_name}@active')
        assert(success)
    # wait for regpeerkey to be final
    for nodeId in range(0, producerNodes):
        Utils.Print("Wait for last regpeerkey to be final on ", nodeId)
        cluster.getNode(nodeId).waitForTransFinalization(trans['transaction_id'])

    Print("relaunch with p2p-bp-gossip-endpoint to enable BP gossip")
    for nodeId in range(0, producerNodes):
        Utils.Print(f"Relaunch node {nodeId} with p2p-bp-gossip-endpoint")
        node = cluster.getNode(nodeId)
        node.kill(signal.SIGTERM)
        producer_name = "defproducer" + chr(ord('a') + nodeId)
        server_address = getHostName(nodeId)
        if not node.relaunch(chainArg=f" --enable-stale-production --p2p-bp-gossip-endpoint {producer_name},{server_address},127.0.0.1"):
            errorExit(f"Failed to relaunch node {nodeId}")

    Print("Wait for messages to be gossiped")
    cluster.getNode(producerNodes-1).waitForHeadToAdvance(blocksToAdvance=60)
    blockNum = cluster.getNode(0).getBlockNum()
    for nodeId in range(0, producerNodes):
        Utils.Print(f"Wait for block ${blockNum} on node ", nodeId)
        cluster.getNode(nodeId).waitForBlock(blockNum)

    # return the peer names (defproducera) of the connected peers in the v1/net/connections JSON
    def connectedPeers(nodeId, connectionsJson):
        peers = []
        for conn in connectionsJson["payload"]:
            if conn["is_socket_open"] is False:
                continue
            peer_addr = conn["peer"]
            if len(peer_addr) == 0:
                if not conn["last_handshake"]["p2p_address"]:
                    continue
                peer_addr = conn["last_handshake"]["p2p_address"].split()[0]
                if not peer_addr:
                    continue
            if peer_names[peer_addr] != "bios" and peer_addr != getHostName(nodeId):
                if conn["is_bp_peer"]:
                    peers.append(peer_names[peer_addr])
        return peers

    def verifyGossipConnections(scheduled_producers):
        assert(len(scheduled_producers) > 0)
        scheduled_producers.sort()
        connection_failure = False
        for nodeId in range(0, producerNodes):
            name = "defproducer" + chr(ord('a') + nodeId)
            if name not in scheduled_producers:
                break
            # retrieve the connections in each node and check if each connects to the other bps in the schedule
            connections = cluster.nodes[nodeId].processUrllibRequest("net", "connections")
            if Utils.Debug: Utils.Print(f"v1/net/connections: {connections}")
            bp_peers = cluster.nodes[nodeId].processUrllibRequest("net", "bp_gossip_peers")
            if Utils.Debug: Utils.Print(f"v1/net/bp_gossip_peers: {bp_peers}")
            peers = connectedPeers(nodeId, connections)
            if not peers:
                Utils.Print(f"ERROR: found no connected peers for node {nodeId}")
                connection_failure = True
                break
            peers.append(name) # add ourselves so matches schedule_producers
            peers = list(set(peers))
            peers.sort()
            if peers != scheduled_producers:
                Utils.Print(f"ERROR: expect {name} has connections to {scheduled_producers}, got connections to {peers}")
                connection_failure = True
                break
            num_peers_found = 0
            for p in bp_peers["payload"]:
                if p["producer_name"] not in peers:
                    Utils.Print(f"ERROR: expect bp peer {p} in peer list")
                    connection_failure = True
                    break
                else:
                    num_peers_found += 1

            assert(num_peers_found == len(peers))
        return not connection_failure

    Print("Verify gossip connections")
    scheduled_producers = cluster.nodes[0].getProducerSchedule()
    success = verifyGossipConnections(scheduled_producers)
    assert(success)

    Print("Manual connect node_03 defproducerd to node_04 defproducere")
    cluster.nodes[3].processUrllibRequest("net", "connect", payload="localhost:9880", exitOnError=True)

    Print("Set new producers b,h,m,r")
    for account in voteAccounts:
        trans=cluster.biosNode.vote(account, ["defproducerb", "defproducerh", "defproducerm", "defproducerr"], silentErrors=False, exitOnError=True)
    cluster.biosNode.getNextCleanProductionCycle(trans)

    Print("Verify new gossip connections")
    scheduled_producers = cluster.nodes[0].getProducerSchedule()
    Print(f"Scheduled producers: {scheduled_producers}")
    assert(len(scheduled_producers) == 4)
    assert("defproducerb" in scheduled_producers and "defproducerh" in scheduled_producers and "defproducerm" in scheduled_producers and "defproducerr" in scheduled_producers)
    success = verifyGossipConnections(scheduled_producers)
    assert(success)

    Print("Verify manual connection still connected")
    connections = cluster.nodes[3].processUrllibRequest("net", "connections")
    if Utils.Debug: Utils.Print(f"v1/net/connections: {connections}")
    found = connectedPeers(3, connections)
    Print(f"Found connections of Node_03: {found}")
    assert("defproducere" in found)     # still connected to manual connection
    assert("defproducerh" not in found) # not connected to new schedule

    testSuccessful = success

finally:
    TestHelper.shutdown(
        cluster,
        walletMgr,
        testSuccessful,
        dumpErrorDetails
    )

exitCode = 0 if testSuccessful else 1
exit(exitCode)
