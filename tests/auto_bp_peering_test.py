#!/usr/bin/env python3

import socket

from TestHarness import Cluster, TestHelper, Utils, WalletMgr

###############################################################
# auto_bp_peering_test
#
# This test sets up  a cluster with 21 producers nodeos, each nodeos is configured with only one producer and only connects to the bios node.
# Moreover, each producer nodeos is also configured with a list of p2p-auto-bp-peer so that each one can automatically establish p2p connections to
# other bps. Test verifies connections are established when producer schedule is active.
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
    "--activate-if",
    "--dump-error-details",
    "--leave-running",
    "--keep-logs",
    "--unshared"
})

Utils.Debug = args.v
activateIF=args.activate_if
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

auto_bp_peer_args = ""
for nodeId in range(0, producerNodes):
    producer_name = "defproducer" + chr(ord('a') + nodeId)
    port = cluster.p2pBasePort + nodeId
    hostname = getHostName(nodeId)
    peer_names[hostname] = producer_name
    auto_bp_peer_args += (" --p2p-auto-bp-peer " + producer_name + "," + hostname)


peer_names["localhost:9776"] = "bios"

testSuccessful = False
try:
    specificNodeosArgs = {}
    for nodeId in range(0, producerNodes):
        specificNodeosArgs[nodeId] = auto_bp_peer_args

    specificNodeosArgs[5] = specificNodeosArgs[5] + ' --p2p-server-address ext-ip0:9999'

    TestHelper.printSystemInfo("BEGIN")
    cluster.launch(
        prodCount=producerCountInEachNode,
        totalNodes=totalNodes,
        pnodes=producerNodes,
        totalProducers=producerNodes,
        activateIF=activateIF,
        topo="./tests/auto_bp_peering_test_shape.json",
        extraNodeosArgs=" --plugin eosio::net_api_plugin ",
        specificExtraNodeosArgs=specificNodeosArgs,
    )

    # wait until produceru is seen by every node
    for nodeId in range(0, producerNodes):
        Utils.Print("Wait for node ", nodeId)
        cluster.nodes[nodeId].waitForProducer("defproduceru", exitOnError=True, timeout=300)

    # retrieve the producer stable producer schedule
    scheduled_producers = []
    schedule = cluster.nodes[0].processUrllibRequest("chain", "get_producer_schedule")
    for prod in schedule["payload"]["active"]["producers"]:
        scheduled_producers.append(prod["producer_name"])
    scheduled_producers.sort()

    connection_failure = False
    for nodeId in range(0, producerNodes):
        # retrieve the connections in each node and check if each connects to the other bps in the schedule
        connections = cluster.nodes[nodeId].processUrllibRequest("net", "connections")
        if Utils.Debug: Utils.Print(f"Node {nodeId} connections {connections}")
        peers = []
        for conn in connections["payload"]:
            if conn["is_socket_open"] is False:
                continue
            peer_addr = conn["peer"]
            if len(peer_addr) == 0:
                if len(conn["last_handshake"]["p2p_address"]) == 0:
                    continue
                peer_addr = conn["last_handshake"]["p2p_address"].split()[0]
            if peer_names[peer_addr] != "bios" and peer_addr != getHostName(nodeId):
                if conn["is_bp_peer"]:
                    peers.append(peer_names[peer_addr])

        if not peers:
            Utils.Print(f"ERROR: found no connected peers for node {nodeId}")
            connection_failure = True
            break
        name = "defproducer" + chr(ord('a') + nodeId)
        peers.append(name) # add ourselves so matches schedule_producers
        peers = list(set(peers))
        peers.sort()
        if peers != scheduled_producers:
            Utils.Print(f"ERROR: expect {name} has connections to {scheduled_producers}, got connections to {peers}")
            connection_failure = True
            break

    testSuccessful = not connection_failure

finally:
    TestHelper.shutdown(
        cluster,
        walletMgr,
        testSuccessful,
        dumpErrorDetails
    )

exitCode = 0 if testSuccessful else 1
exit(exitCode)
