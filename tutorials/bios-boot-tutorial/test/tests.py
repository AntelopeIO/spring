import subprocess
import re
import os
import pytest
import json

############# HELPER FUNCTIONS #########################
def create_key():
    result = subprocess.run(
        ["cleos", "create", "key", "--to-console"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    if result.returncode != 0:
        raise RuntimeError(f"cleos create key failed:\n{result.stderr}")

    # Match both keys
    private_key_match = re.search(r"Private key: ([A-Za-z0-9]+)", result.stdout)
    public_key_match = re.search(r"Public key: ([A-Za-z0-9]+)", result.stdout)

    if not private_key_match or not public_key_match:
        raise ValueError(f"Failed to parse keys from output:\n{result.stdout}")

    private_key = private_key_match.group(1)
    public_key = public_key_match.group(1)

    return public_key, private_key

def get_keys_for_user(account_name, user_accounts):
    for user in user_accounts:
        if user["name"] == account_name:
            return user["pub"], user["pvt"]
    raise ValueError(f"Account '{account_name}' not found in user_accounts.")

def import_key(priv_key):
    import_key_command = [
        "cleos",
        "wallet", "import", "--private-key",priv_key
    ]

    result = subprocess.run(import_key_command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    if result.returncode != 0:
        raise RuntimeError(f"cleos wallet import failed:\n{result.stderr}")

############### SESSION SCOPE ###########################
@pytest.fixture(scope="session")
def user_accounts():
    with open("accounts.json") as f:
        data = json.load(f)
    return data["users"]

############## TESTS ####################################

##
# test transfer SYS between accounts 
##
@pytest.mark.parametrize("from_account,to_account", [
    ("useraaaaaljm", "useraaaaalla"),
])
def test_cleos_transfer_sys(from_account, to_account, user_accounts):
    endpoint = os.environ.get("ENDPOINT")
    assert endpoint, "Environment variable ENDPOINT must be set."
    
    # Get from_account public and private key from helper
    from_pub_key, from_priv_key = get_keys_for_user(from_account, user_accounts)
    # add to Wallet
    import_key(from_priv_key)

    transfer_data = {
        "from": from_account,
        "to": to_account,
        "quantity": "1.0000 SYS",
        "memo": "init"
    }

    cleos_command = [
        "cleos",
        "-u", endpoint,
        "push", "action",
        "eosio.token",
        "transfer",
        str(transfer_data).replace("'", '"'),  # Ensure proper JSON formatting
        "-p", f"{from_account}@active"
    ]

    result = subprocess.run(
        cleos_command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # stderr validation
    stderr_pattern = r"executed transaction: [0-9a-f]+  \d+ bytes  \d+ us"
    assert re.search(stderr_pattern, result.stderr), f"stderr did not match expected pattern:\n{result.stderr}"

    # stdout validation
    expected_stdout_patterns = [
        fr'#   eosio\.token <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 SYS","memo":"init"}}',
        fr'#  {from_account} <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 SYS","memo":"init"}}',
        fr'#  {to_account} <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 SYS","memo":"init"}}'
    ]

    stdout_lines = result.stdout.strip().splitlines()
    matched_count = 0

    for pattern in expected_stdout_patterns:
        if any(re.match(pattern, line.strip()) for line in stdout_lines):
            matched_count += 1

    assert matched_count == 3, f"Expected 3 matching lines in stdout, found {matched_count}:\n{result.stdout}"

##
# Test Create New User
##
@pytest.mark.parametrize("payer_account,new_account", [
    ("useraaaaaaae", "testnewusere"),  # Change these as needed
])
def test_cleos_newaccount_output(payer_account, new_account,user_accounts):
    endpoint = os.environ.get("ENDPOINT")
    assert endpoint, "Environment variable ENDPOINT must be set."

    # Get payer's public and private key from helper
    payer_pub_key, payer_priv_key = get_keys_for_user(payer_account, user_accounts)
    # add to Wallet
    import_key(payer_priv_key)

    # Generate Keys
    pub, priv = create_key()
    # add to Wallet
    import_key(priv)

    cleos_command = [
        "cleos",
        "-u", endpoint,
        "system", "newaccount",
        payer_account,
        new_account,
        pub,
        pub,
        "--stake-net", "1.00 SYS",
        "--stake-cpu", "1.00 SYS",
        "--buy-ram-bytes", "3000",
        "-p", f"{payer_account}@active"
    ]

    result = subprocess.run(
        cleos_command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # --- stderr validation ---
    stderr_pattern = r"executed transaction: [0-9a-f]+  \d+ bytes  \d+ us"
    assert re.search(stderr_pattern, result.stderr), f"stderr did not match expected pattern:\n{result.stderr}"

    # --- stdout length check ---
    stdout_lines = result.stdout.strip().splitlines()
    assert len(stdout_lines) > 20, f"Expected more than 20 lines in stdout, got {len(stdout_lines)}:\n{result.stdout}"

##
# test swap SYS -> A between accounts 
##
@pytest.mark.parametrize("from_account", [
    "useraaaaaljm",
])
def test_cleos_transfer_sys(from_account, user_accounts):
    endpoint = os.environ.get("ENDPOINT")
    assert endpoint, "Environment variable ENDPOINT must be set."
    
    # Get from_account public and private key from helper
    from_pub_key, from_priv_key = get_keys_for_user(from_account, user_accounts)
    # add to Wallet
    import_key(from_priv_key)

    transfer_data = {
        "from": from_account,
        "to": "core.vaulta",
        "quantity": "1.0000 SYS",
        "memo": "init"
    }

    cleos_command = [
        "cleos",
        "-u", endpoint,
        "push", "action",
        "eosio.token",
        "transfer",
        str(transfer_data).replace("'", '"'),  # Ensure proper JSON formatting
        "-p", f"{from_account}@active"
    ]

    result = subprocess.run(
        cleos_command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # stderr validation
    stderr_pattern = r"executed transaction: [0-9a-f]+  \d+ bytes  \d+ us"
    assert re.search(stderr_pattern, result.stderr), f"stderr did not match expected pattern:\n{result.stderr}"

    # stdout validation
    expected_stdout_patterns = [
        fr'#   eosio\.token <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 SYS","memo":"init"}}',
        fr'#  {from_account} <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 SYS","memo":"init"}}',
        fr'#  {to_account} <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 SYS","memo":"init"}}'
    ]

    stdout_lines = result.stdout.strip().splitlines()
    matched_count = 0

    for pattern in expected_stdout_patterns:
        if any(re.match(pattern, line.strip()) for line in stdout_lines):
            matched_count += 1

    assert matched_count == 3, f"Expected 3 matching lines in stdout, found {matched_count}:\n{result.stdout}"