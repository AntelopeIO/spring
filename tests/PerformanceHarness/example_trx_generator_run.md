
## Command
Sleep is needed otherwise subjective transaction may reject transactions as too soon.
```
CHAIN_ID=$(cleos --url $HTTP_URL get info | grep chain_id | cut -d:  -f2 | sed 's/[ ",]//g')
LIB_ID=$(cleos --url $HTTP_URL get info | grep last_irreversible_block_id | cut -d:  -f2 | sed 's/[ ",]//g')
sleep 3
${BUILD_TEST_DIR}/trx_generator/trx_generator --generator-id $GENERATORID \
     --chain-id $CHAIN_ID \
     --contract-owner-account eosio \
     --accounts $COMMA_SEP_ACCOUNTS \
     --priv-keys $COMMA_SEP_KEYS \
     --last-irreversible-block-id $LIB_ID \
     --log-dir $TRX_LOG_DIR \
     --peer-endpoint-type p2p \
     --peer-endpoint $P2P_HOST \
     --port $PEER2PEERPORT
```

## Output
Why is this 0us
```
info  2024-06-12T21:14:31.379 trx_gener main.cpp:128                  main                 ] Initializing accounts. Attempt to create name for purplepurple
info  2024-06-12T21:14:31.379 trx_gener main.cpp:128                  main                 ] Initializing accounts. Attempt to create name for orangeorange
info  2024-06-12T21:14:31.379 trx_gener main.cpp:128                  main                 ] Initializing accounts. Attempt to create name for pinkpink1111
info  2024-06-12T21:14:31.379 trx_gener main.cpp:128                  main                 ] Initializing accounts. Attempt to create name for blueblue1111
info  2024-06-12T21:14:31.379 trx_gener main.cpp:128                  main                 ] Initializing accounts. Attempt to create name for yellowyellow
info  2024-06-12T21:14:31.379 trx_gener main.cpp:128                  main                 ] Initializing accounts. Attempt to create name for greengreen11
info  2024-06-12T21:14:31.379 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.379 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.379 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.379 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.379 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.379 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.379 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.379 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.379 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.380 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.380 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.380 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.380 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.380 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.380 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.380 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.380 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.380 trx_gener main.cpp:141                  main                 ] Initializing private keys. Attempt to create private_key for 5JX...ZZZ : gen key 5JX...ZZZ
info  2024-06-12T21:14:31.380 trx_gener main.cpp:189                  main                 ] Initial Trx Generator config:  generator id: 0 chain id: b836a453e919f3dc7be91cd60cb949c625af7dc0f0a1f129f84763238d8bbc50 contract owner account: eosio trx expiration seconds: 3600 lib id: 0010f26923a1c9b094a778c2e58a19808af8ebaaa4346d72e1d3d19842fa5d2c log dir: /bigata1/log/trx_generator stop on trx failed: 1
info  2024-06-12T21:14:31.380 trx_gener main.cpp:190                  main                 ] Initial Provider config: Provider base config endpoint type: p2p peer_endpoint: p2p.spring-beta2.jungletestnet.io port: 9898 api endpoint: /v1/chain/send_transaction2
info  2024-06-12T21:14:31.380 trx_gener main.cpp:191                  main                 ] Initial Accounts config: Accounts Specified: accounts: [ purplepurple, orangeorange, pinkpink1111, blueblue1111, yellowyellow, greengreen11 ] keys: [ 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ, 5JX...ZZZ ]
info  2024-06-12T21:14:31.380 trx_gener main.cpp:192                  main                 ] Transaction TPS Tester config: Trx Tps Tester Config: duration: 60 target tps: 1
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:103         setup                ] Stop Generation (form potential ongoing generation in preparation for starting new generation run).
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:342         stop_generation      ] Stopping transaction generation
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:106         setup                ] Create All Initial Transfer Action/Reaction Pairs (acct 1 -> acct 2, acct 2 -> acct 1) between all provided accounts.
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from purplepurple to orangeorange
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from orangeorange to purplepurple
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from purplepurple to pinkpink1111
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from pinkpink1111 to purplepurple
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from purplepurple to blueblue1111
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from blueblue1111 to purplepurple
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from purplepurple to yellowyellow
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from yellowyellow to purplepurple
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from purplepurple to greengreen11
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from greengreen11 to purplepurple
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from orangeorange to pinkpink1111
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from pinkpink1111 to orangeorange
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from orangeorange to blueblue1111
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from blueblue1111 to orangeorange
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from orangeorange to yellowyellow
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from yellowyellow to orangeorange
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from orangeorange to greengreen11
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from greengreen11 to orangeorange
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from pinkpink1111 to blueblue1111
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from blueblue1111 to pinkpink1111
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from pinkpink1111 to yellowyellow
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from yellowyellow to pinkpink1111
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from pinkpink1111 to greengreen11
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from greengreen11 to pinkpink1111
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from blueblue1111 to yellowyellow
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from yellowyellow to blueblue1111
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from blueblue1111 to greengreen11
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from greengreen11 to blueblue1111
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:74          create_initial_trans ] create_initial_transfer_actions: creating transfer from yellowyellow to greengreen11
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:79          create_initial_trans ] create_initial_transfer_actions: creating transfer from greengreen11 to yellowyellow
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:87          create_initial_trans ] create_initial_transfer_actions: total action pairs created: 15
info  2024-06-12T21:14:31.380 trx_gener trx_generator.cpp:109         setup                ] Create All Initial Transfer Transactions (one for each created action).
info  2024-06-12T21:14:31.382 trx_gener trx_generator.cpp:112         setup                ] Setup p2p transaction provider
info  2024-06-12T21:14:31.382 trx_gener trx_generator.cpp:114         setup                ] Update each trx to qualify as unique and fresh timestamps, re-sign trx, and send each updated transactions via p2p transaction provider
info  2024-06-12T21:14:31.382 trx_gener trx_provider.cpp:71           connect              ] Attempting P2P connection to p2p.spring-beta2.jungletestnet.io:9898.
info  2024-06-12T21:14:31.750 trx_gener trx_provider.cpp:75           connect              ] Connected to p2p.spring-beta2.jungletestnet.io:9898.
info  2024-06-12T21:15:30.751 trx_gener trx_provider.cpp:84           disconnect           ] disconnect waiting on ack - sent 60 | acked 59 | waited 0
info  2024-06-12T21:15:31.751 trx_gener trx_generator.cpp:292         tear_down            ] Sent transactions: 60
info  2024-06-12T21:15:31.751 trx_gener trx_generator.cpp:293         tear_down            ] Tear down p2p transaction provider
info  2024-06-12T21:15:31.751 trx_gener trx_generator.cpp:296         tear_down            ] Stop Generation.
info  2024-06-12T21:15:31.751 trx_gener trx_generator.cpp:342         stop_generation      ] Stopping transaction generation
info  2024-06-12T21:15:31.751 trx_gener trx_generator.cpp:345         stop_generation      ] 60 transactions executed, 0.00000000000000000us / transaction
info  2024-06-12T21:15:31.751 trx_gener main.cpp:226                  main                 ] Exiting main SUCCESS
```
