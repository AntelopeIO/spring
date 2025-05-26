import subprocess
import string
import re
import os
import pytest
import json

############# CONTEXT          #########################
CURRENCY_SYMBOL = "EOS"
DEFAULT_ENDPOINT = "127.0.0.1:8000"

############# HELPER FUNCTIONS #########################

# Create New Key
def create_key():
    result = subprocess.run(
        ["cleos", "create", "key", "--r1", "--to-console"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    if result.returncode != 0:
        raise RuntimeError(f"cleos create key failed:\n{result.stderr}")

    # Match both keys
    private_key_match = re.search(r"Private key: ([A-Za-z0-9_]+)", result.stdout)
    public_key_match = re.search(r"Public key: ([A-Za-z0-9_]+)", result.stdout)

    if not private_key_match or not public_key_match:
        raise ValueError(f"Failed to parse keys from output:\n{result.stdout}")

    private_key = private_key_match.group(1)
    public_key = public_key_match.group(1)

    return public_key, private_key

# Check Nodeos has account
def check_user_exists(account_name, endpoint):
    result = subprocess.run(
        ["cleos", "--url", endpoint, "get", "account", account_name],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0

# Return all the keys in wallet
def get_wallet_keys():
    result = subprocess.run(
        "cleos wallet keys list | jq",
        shell=True,
        capture_output=True,
        text=True
    )
    
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as e:
        raise ValueError(f"Failed to parse JSON: {e}\nOutput was:\n{result.stdout}")

# Return Keys from the account.json file that is part of bios-boot-tutorial
# account.json is read as part of session function
def get_keys_for_user(account_name, user_accounts):
    for user in user_accounts:
        if user["name"] == account_name:
            return user["pub"], user["pvt"]
    raise ValueError(f"Account '{account_name}' not found in user_accounts.")

# Add A Key Pair to the Default Wallet
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
def currency_symbol():
    return CURRENCY_SYMBOL
    
@pytest.fixture(scope="session")
def endpoint():
    value = os.environ.get("ENDPOINT")
    if not value:
        print(f"[pytest] Warning: env variable ENDPOINT not set. Using default: {DEFAULT_ENDPOINT}")
    return value or DEFAULT_ENDPOINT
    
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
def test_cleos_transfer_currency(
    from_account,
    to_account,
    endpoint,
    user_accounts,
    currency_symbol):
    assert endpoint, "ENDPOINT variable must be set."
    
    # get transaction signing key for from account user
    sign_pub_key, sign_priv_key = get_keys_for_user(from_account, user_accounts)
    # check if key is already in wallet 
    wallet_pub_keys = get_wallet_keys()
    # import key if it is not in wallet
    if not sign_pub_key in wallet_pub_keys:
        import_key(sign_priv_key)

    transfer_data = {
        "from": from_account,
        "to": to_account,
        "quantity": f"1.0000 {currency_symbol}",
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
        fr'#   eosio\.token <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 {currency_symbol}","memo":"init"}}',
        fr'#  {from_account} <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 {currency_symbol}","memo":"init"}}',
        fr'#  {to_account} <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 {currency_symbol}","memo":"init"}}'
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
@pytest.mark.parametrize("payer_account", [
    ("useraaaaaaaa"),  # Change these as needed
])
def test_newaccount(payer_account,endpoint,user_accounts,currency_symbol):
    for single_char in string.ascii_lowercase:
        new_account = f'testnewuser{single_char}'
        if not check_user_exists(new_account, endpoint):
            cleos_newaccount_output(payer_account,
                new_account,
                endpoint,
                user_accounts,
                currency_symbol)
            break

def cleos_newaccount_output(
    payer_account,
    new_account,
    endpoint,
    user_accounts,
    currency_symbol):

    assert endpoint, "ENDPOINT variable must be set."

    # get transaction signing key for from account user
    sign_pub_key, sign_priv_key = get_keys_for_user(payer_account, user_accounts)
    # check if key is already in wallet 
    wallet_pub_keys = get_wallet_keys()
    # import key if it is not in wallet
    if not sign_pub_key in wallet_pub_keys:
        import_key(sign_priv_key)

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
        "--stake-net", f"1.00 {currency_symbol}",
        "--stake-cpu", f"1.00 {currency_symbol}",
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
def test_cleos_transfer_vaulta_currency(
    from_account,
    user_accounts,
    endpoint,
    currency_symbol):
    
    # All system user keys already imported into wallet 
    to_account = "core.vaulta"

    transfer_data = {
        "from": from_account,
        "to": to_account,
        "quantity": f"1.0000 {currency_symbol}",
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
        fr'#   eosio\.token <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 {currency_symbol}","memo":"init"}}',
        fr'#  {from_account} <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 {currency_symbol}","memo":"init"}}',
        fr'#  {to_account} <= eosio\.token::transfer\s+{{"from":"{from_account}","to":"{to_account}","quantity":"1.0000 {currency_symbol}","memo":"init"}}',
        fr'#  {to_account} <= {to_account}::transfer\s+{{"from":"{to_account}","to":"{from_account}","quantity":"1.0000 A","memo":""}}',
        fr'#  {from_account} <= {to_account}::transfer\s+{{"from":"{to_account}","to":"{from_account}","quantity":"1.0000 A","memo":""}}',
    ]

    stdout_lines = result.stdout.strip().splitlines()
    matched_count = 0

    for pattern in expected_stdout_patterns:
        if any(re.match(pattern, line.strip()) for line in stdout_lines):
            matched_count += 1

    assert matched_count == 3, f"Expected 3 matching lines in stdout, found {matched_count}:\n{result.stdout}"