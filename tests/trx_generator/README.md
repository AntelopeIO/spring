# Transaction Generator

The Transaction Generator is a program built to create and send transactions at a specified rate in order to generate load on a blockchain. It is comprised of 3 main components: Transaction Generator, Transaction Provider, and Performance Monitor.

The `trx_generator.[hpp, cpp]` is currently specialized to be a `transfer_trx_generator` primarily focused on generating token transfer transactions. The transactions are then provided to the network by the `trx_provider.[hpp, cpp]` which is currently aimed at the P2P network protocol in the `p2p_trx_provider`. The third component, the `tps_performance_monitor`, allows the Transaction Generator to monitor its own performance and take action to notify and exit if it is unable to keep up with the requested transaction generation rate.

The Transaction Generator logs each transaction's id and sent timestamp at the moment the Transaction Provider sends the transaction. Logs are written to the configured log directory and will follow the naming convention `trx_data_output_10744.txt` where `10744` is the transaction generator instance's process ID.

## Configuration Options

`./build/tests/trx_generator/trx_generator` can be configured using the following command line arguments:

<details open>
    <summary>Expand Argument List</summary>

- `--generator-id arg` (=0) Id for the transaction generator.
  Allowed range (0-960). Defaults to 0.
- `--chain-id arg` set the chain id
- `--contract-owner-account arg` Account name of the contract account
  for the transaction actions
- `--accounts arg` comma-separated list of accounts that
  will be used for transfers. Minimum
  required accounts: 2.
- `--priv-keys arg` comma-separated list of private keys in
  same order of accounts list that will
  be used to sign transactions. Minimum
  required: 2.
- `--trx-expiration arg` (=3600) transaction expiration time in seconds.
  Defaults to 3,600. Maximum allowed:
  3,600
- `--trx-gen-duration arg` (=60) Transaction generation duration
  (seconds). Defaults to 60 seconds.
- `--target-tps arg` (=1) Target transactions per second to
  generate/send. Defaults to 1
  transaction per second.
- `--last-irreversible-block-id arg` Current last-irreversible-block-id (LIB
  ID) to use for transactions.
- `--monitor-spinup-time-us arg` (=1000000)
  Number of microseconds to wait before
  monitoring TPS. Defaults to 1000000
  (1s).
- `--monitor-max-lag-percent arg` (=5) Max percentage off from expected
  transactions sent before being in
  violation. Defaults to 5.
- `--monitor-max-lag-duration-us arg` (=1000000)
  Max microseconds that transaction
  generation can be in violation before
  quitting. Defaults to 1000000 (1s).
- `--log-dir arg` set the logs directory
- `--stop-on-trx-failed arg` (=1) stop transaction generation if sending
  fails.
- `--abi-file arg` The path to the contract abi file to
  use for the supplied transaction action
  data
- `--actions-data arg` The json actions data file or json
  actions data description string to use
- `--actions-auths arg` The json actions auth file or json
  actions auths description string to
  use, containting authAcctName to
  activePrivateKey pairs.
- `--peer-endpoint arg` (=127.0.0.1) set the peer endpoint to send
  transactions to
- `--port arg` (=9876) set the peer endpoint port to send
  transactions to
- `-h [ --help ]` print this list
</details>

## Running Other Actions

Instructions and examples for running `trx_generator` to create transactions with the custom action instead of the default transfer actions. The tool uses the ABI to serialize the action and sign the transactions with the provided private keys. If you need to generate unique account names per transaction (e.g., for account creation actions), the tool supports placeholders like `ACCT_PER_TRX` in the action, which it will replace with generated names.

1. **Prepare the ABI File**: Specify the path to the ABI file, using the `--abi-file` option. This file describes the structure of the actions available in the contract.

   Example: `--abi-file path/to/contract.abi`

2. **Define Action Data**: Use the `--actions-data` option to provide a JSON file or string that describes the action(s) you want to include in the transactions. This JSON should specify the action name, the account associated with the action, and the data payload for the action as per the ABI. The `actionAuthAcct` and `authorization:actor` specify the account that signs and executes the action, typically using the same account name, and do not reference the contract owner's account, which is specified separately via the `--contract-owner-account` option.

   Example JSON structure for action data:

   ```json
   [
     {
       "actionName": "youraction",
       "actionAuthAcct": "accountname",
       "actionData": {
         "param1": "value1",
         "param2": "value2"
       },
       "authorization": {
         "actor": "accountname",
         "permission": "active"
       }
     }
   ]
   ```

   This may be passed directly as a string or passed as path to a file with the same information:
   `--actions-data path/to/actions.json` or `--actions-data '[{"actionName": "youraction", ...}]'`

3. **Specify Authorization Keys**: Provide the private key needed to sign the transaction on on the user's behalf. This is done with the `--actions-auths` option. It's a JSON file that maps the account name to the corresponding private key. The `accountname` here must match the `authorization:actor@permission` specified in the `action-data` JSON.

   Example JSON structure for auths:

   ```json
   {
     "accountname": "privatekeystring"
   }
   ```

   The JSON can include multiple account names mapped to their respective private keys if needed.

   ```json
   {
     "accountname1": "privatekeystring1",
     "accountname2": "privatekeystring2"
   }
   ```

   This may be passed as a string or file path:
   `--actions-auths path/to/auths.json` or `--actions-auths '{"accountname": "privatekeystring"}'`

4. **Set Contract Owner Account**: Ensure the `--contract-owner-account` option is set for the action specified in the `action-data` JSON.

   Example: `--contract-owner-account contractaccount`

5. **Run the Generator**: Execute the `trx_generator` with these options along with other necessary configurations like `--chain-id`, `--last-irreversible-block-id`, `--target-tps`, etc., to generate and send transactions with your custom action.

   Full command example:

   ```bash
   ./build/tests/trx_generator/trx_generator --generator-id 1 --chain-id yourchainid --contract-owner-account contractaccount --abi-file path/to/contract.abi --actions-data path/to/actions.json --actions-auths path/to/auths.json --last-irreversible-block-id yourlibid --target-tps 10 --trx-gen-duration 60 --log-dir logs
   ```
