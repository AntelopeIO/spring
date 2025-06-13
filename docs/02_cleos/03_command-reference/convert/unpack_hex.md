## Description

From packed HEX to json form

## Positionals

- `hex` _TEXT_ - The packed HEX of built-in types including: signed_block, transaction/action_trace, transaction, action, abi_def

## Options

- `-h,--help` - Print this help message and exit
- `--type` - Type of the HEX data, if not specified then some common types are attempted.
- `--abi-file` - The abi file that contains --type for unpacking

## Usage

```sh
cleos convert unpack_hex da9fbe9042e1bc9bd64d7a4506534d492107a29f79ad671c1fea19ae3fb70eb403000000023b3d4b01000000038159f32d3c8073a6a2f1ad5aa26e2e804a7815c2b5ef729e84fb803101006400000000000000000000000000000000000000000001010000010000000000ea3055ccfe3b56076237b0b6da2f580652ee1420231b96d3d96b28183769ac932c9e5902000000000000000200000000000000010000000000ea3055020000000000000000000000000000ea30550000000000ea305500000000221acfa4010000000000ea305500000000a8ed32329801013b3d4b0000000000ea30550000000000015ab65a885a31e441ac485ebd2aeba87bf7ee6e7bcc40bf3a24506ba1000000000000000000000000000000000000000000000000000000000000000062267e8b11d7d8f28e1f991a4de2b08cf92500861af2795765bdc9263cd6f4cd000000000001000021010ec7e080177b2c02b278d5088611686b49d739925a92d9bfcacd7fc6b74053bd00000000000000000000da9fbe9042e1bc9bd64d7a4506534d492107a29f79ad671c1fea19ae3fb70eb403000000023b3d4b01000000038159f32d3c8073a6a2f1ad5aa26e2e804a7815c2b5ef729e84fb803100000000000000000000
```

## Output


```json
{
  "id": "da9fbe9042e1bc9bd64d7a4506534d492107a29f79ad671c1fea19ae3fb70eb4",
  "block_num": 3,
  "block_time": "2020-01-01T00:00:01.000",
  "producer_block_id": "000000038159f32d3c8073a6a2f1ad5aa26e2e804a7815c2b5ef729e84fb8031",
  "receipt": {
    "status": "executed",
    "cpu_usage_us": 100,
    "net_usage_words": 0
  },
  "elapsed": 0,
  "net_usage": 0,
  "scheduled": false,
  "action_traces": [{
    "action_ordinal": 1,
    "creator_action_ordinal": 0,
    "closest_unnotified_ancestor_action_ordinal": 0,
    "receipt": {
      "receiver": "eosio",
      "act_digest": "ccfe3b56076237b0b6da2f580652ee1420231b96d3d96b28183769ac932c9e59",
      "global_sequence": 2,
      "recv_sequence": 2,
      "auth_sequence": [[
        "eosio",
        2
      ]
      ],
      "code_sequence": 0,
      "abi_sequence": 0
    },
    "receiver": "eosio",
    "act": {
      "account": "eosio",
      "name": "onblock",
      "authorization": [{
        "actor": "eosio",
        "permission": "active"
      }
      ],
      "data": "013b3d4b0000000000ea30550000000000015ab65a885a31e441ac485ebd2aeba87bf7ee6e7bcc40bf3a24506ba1000000000000000000000000000000000000000000000000000000000000000062267e8b11d7d8f28e1f991a4de2b08cf92500861af2795765bdc9263cd6f4cd000000000001000021010ec7e080177b2c02b278d5088611686b49d739925a92d9bfcacd7fc6b74053bd"
    },
    "context_free": false,
    "elapsed": 0,
    "console": "",
    "trx_id": "da9fbe9042e1bc9bd64d7a4506534d492107a29f79ad671c1fea19ae3fb70eb4",
    "block_num": 3,
    "block_time": "2020-01-01T00:00:01.000",
    "producer_block_id": "000000038159f32d3c8073a6a2f1ad5aa26e2e804a7815c2b5ef729e84fb8031",
    "account_ram_deltas": [],
    "return_value": ""
  }
  ],
  "failed_dtrx_trace": null
}
```
