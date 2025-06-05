## Description

The `net_plugin` provides an authenticated p2p protocol to persistently synchronize nodes.

## Usage

```console
# config.ini
plugin = eosio::net_plugin
[options]
```
```sh
# command-line
nodeos ... --plugin eosio::net_plugin [options]
```

## Options

These can be specified from both the `nodeos` command-line or the `config.ini` file:

```console
Config Options for eosio::net_plugin:
  --p2p-listen-endpoint arg (=0.0.0.0:9876:0)
                                        The actual host:port[:trx|:blk][:<rate-
                                        cap>] used to listen for incoming p2p
                                        connections. May be used multiple
                                        times. The optional rate cap will limit
                                        per connection block sync bandwidth to
                                        the specified rate. Total allowed
                                        bandwidth is the rate-cap multiplied by
                                        the connection count limit. A number
                                        alone will be interpreted as bytes per
                                        second. The number may be suffixed with
                                        units. Supported units are: 'B/s',
                                        'KB/s', 'MB/s, 'GB/s', 'TB/s', 'KiB/s',
                                        'MiB/s', 'GiB/s', 'TiB/s'. Transactions
                                        and blocks outside sync mode are not
                                        throttled. The optional 'trx' and 'blk'
                                        indicates to peers that only
                                        transactions 'trx' or blocks 'blk'
                                        should be sent. Examples:
                                           192.168.0.100:9876:1MiB/s
                                           node.eos.io:9876:trx:1512KB/s
                                           node.eos.io:9876:0.5GB/s
                                           [2001:db8:85a3:8d3:1319:8a2e:370:734
                                        8]:9876:250KB/s
  --p2p-server-address arg              An externally accessible host:port for
                                        identifying this node. Defaults to
                                        p2p-listen-endpoint. May be used as
                                        many times as p2p-listen-endpoint. If
                                        provided, the first address will be
                                        used in handshakes with other nodes;
                                        otherwise the default is used.
  --p2p-peer-address arg                The public endpoint of a peer node to
                                        connect to. Use multiple
                                        p2p-peer-address options as needed to
                                        compose a network.
                                         Syntax: host:port[:trx|:blk]
                                         The optional 'trx' and 'blk' indicates
                                        to node that only transactions 'trx' or
                                        blocks 'blk' should be sent. Examples:
                                           p2p.eos.io:9876
                                           p2p.trx.eos.io:9876:trx
                                           p2p.blk.eos.io:9876:blk

  --p2p-max-nodes-per-host arg (=1)     Maximum number of client nodes from any
                                        single IP address
  --p2p-accept-transactions arg (=1)    Allow transactions received over p2p
                                        network to be evaluated and relayed if
                                        valid.
  --p2p-disable-block-nack arg (=0)     Disable block notice and block nack.
                                        All blocks received will be broadcast
                                        to all peers unless already received.
  --p2p-auto-bp-peer arg                The account and public p2p endpoint of
                                        a block producer node to automatically
                                        connect to when it is in producer
                                        schedule proximity
                                        .  Syntax: account,host:port
                                          Example,
                                            eosproducer1,p2p.eos.io:9876
                                            eosproducer2,p2p.trx.eos.io:9876:tr
                                        x
                                            eosproducer3,p2p.blk.eos.io:9876:bl
                                        k

  --p2p-producer-peer arg               Producer peer name of this node used to
                                        retrieve peer key from on-chain
                                        peerkeys table. Private key of peer key
                                        should be configured via
                                        signature-provider.
  --agent-name arg (=EOS Test Agent)    The name supplied to identify this node
                                        amongst the peers.
  --allowed-connection arg (=any)       Can be 'any' or 'producers' or
                                        'specified' or 'none'. If 'specified',
                                        peer-key must be specified at least
                                        once. If only 'producers', peer-key is
                                        not required. 'producers' and
                                        'specified' may be combined.
  --peer-key arg                        Optional public key of peer allowed to
                                        connect.  May be used multiple times.
  --peer-private-key arg                Tuple of [PublicKey, WIF private key]
                                        (may specify multiple times)
  --max-clients arg (=25)               Maximum number of clients from which
                                        connections are accepted, use 0 for no
                                        limit
  --connection-cleanup-period arg (=30) number of seconds to wait before
                                        cleaning up dead connections
  --max-cleanup-time-msec arg (=10)     max connection cleanup time per cleanup
                                        call in milliseconds
  --p2p-dedup-cache-expire-time-sec arg (=10)
                                        Maximum time to track transaction for
                                        duplicate optimization
  --net-threads arg (=4)                Number of worker threads in net_plugin
                                        thread pool
  --sync-fetch-span arg (=1000)         Number of blocks to retrieve in a chunk
                                        from any individual peer during
                                        synchronization
  --sync-peer-limit arg (=3)            Number of peers to sync from
  --use-socket-read-watermark arg (=0)  Enable experimental socket read
                                        watermark optimization
  --peer-log-format arg (=["${_peer}" - ${_cid} ${_ip}:${_port}] )
                                        The string used to format peers when
                                        logging messages about them.  Variables
                                        are escaped with ${<variable name>}.
                                        Available Variables:
                                           _peer  endpoint name

                                           _name  self-reported name

                                           _cid   assigned connection id

                                           _id    self-reported ID (64 hex
                                                  characters)

                                           _sid   first 8 characters of
                                                  _peer.id

                                           _ip    remote IP address of peer

                                           _port  remote port number of peer

                                           _lip   local IP address connected to
                                                  peer

                                           _lport local port number connected
                                                  to peer

                                           _nver  p2p protocol version


  --p2p-keepalive-interval-ms arg (=10000)
                                        peer heartbeat keepalive message
                                        interval in milliseconds
```

## Dependencies

None
