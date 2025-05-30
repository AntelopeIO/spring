## Description
Converts a public key to the legacy key format (EOS prefix for k1 keys, PUB_R1 prefix for r1 keys, PUB_WA prefix for WebAuthn keys)

If the key is already in the legacy format it will be output unchanged.

Only the k1 key format differs between legacy and new formats.

## Positionals

- `public_key` _TEXT_ - The public key to output in legacy format

## Options

- `-h,--help` - Print this help message and exit

## Usage


```sh
 cleos convert print_public_key_as_legacy PUB_K1_6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5BoDq63
```

## Output

```
EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV
```
