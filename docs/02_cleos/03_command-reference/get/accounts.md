## Description
Retrieves all accounts associated with a defined public key

[[info]]
|This command will not return privileged accounts.

## Positional Parameters
`public_key` _TEXT_  - The public key to retrieve accounts for
## Options
- `-j,--json` - Output in JSON format
## Example


```sh
cleos get accounts PUB_K1_8mUftJXepGzdQ2TaCduNuSPAfXJHf22uex4u41ab1EVv7ctUZy
{
  "account_names": [
    "testaccount"
  ]
}
```
