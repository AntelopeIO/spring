## Description
Converts a public key to the canonical key format (PUB_K1 prefix for k1 keys, PUB_R1 prefix for r1 keys, PUB_WA prefix for WebAuthn keys)

If the key is already in the canonical format it will be output unchanged.

Only the k1 key format differs between legacy format and canonical format.

## Positionals

- `public_key` _TEXT_ - The public key to output in new format

## Options

- `-h,--help` - Print this help message and exit

## Usage


```sh
 cleos convert public_key PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63
```

## Output

```
PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63
```
