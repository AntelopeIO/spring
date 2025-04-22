#!/usr/bin/env python3

import os
import re
import tempfile

from TestHarness import Utils

###############################################################
# spring_util_bls_test
#
#  Test spring-util's BLS commands.
#  - Create a key pair
#  - Create a POP (Proof of Possession)
#  - Error handlings
#
###############################################################

Print=Utils.Print
testSuccessful=False

def test_create_key_to_console():
    rslts = Utils.processSpringUtilCmd("bls create key --to-console", "create key to console", silentErrors=False)
    check_create_key_results(rslts)

def test_create_key_to_file():
    with tempfile.NamedTemporaryFile(mode="w+") as tmp_file:
        Utils.processSpringUtilCmd("bls create key --file {}".format(tmp_file.name), "create key to file", silentErrors=False)

        tmp_file.seek(0)
        rslts = tmp_file.read()
        check_create_key_results(rslts)

def test_create_pop_from_command_line():
    # Create a pair of keys
    rslts = Utils.processSpringUtilCmd("bls create key --to-console", "create key to console", silentErrors=False)
    results = get_results(rslts)

    # save results
    private_key = results["Private key"]
    public_key = results["Public key"]
    pop = results["Proof of Possession"]

    # use the private key to create POP
    rslts = Utils.processSpringUtilCmd("bls create pop --private-key {}".format(private_key), "create pop from command line", silentErrors=False)
    results = get_results(rslts)

    # check pop and public key are the same as those generated before
    assert results["Public key"] == public_key
    assert results["Proof of Possession"] == pop

def test_create_pop_from_file():
    # Create a pair of keys
    rslts = Utils.processSpringUtilCmd("bls create key --to-console", "create key to console", silentErrors=False)
    results = get_results(rslts)

    # save results
    private_key = results["Private key"]
    public_key = results["Public key"]
    pop = results["Proof of Possession"]

    # save private key to a file
    with tempfile.NamedTemporaryFile(mode="w+") as private_key_file:
        private_key_file.write(private_key)
        private_key_file.flush()

        # use the private key file to create POP
        rslts = Utils.processSpringUtilCmd("bls create pop --file {}".format(private_key_file.name), "create pop from command line", silentErrors=False)
        results = get_results(rslts)

    # check pop and public key are the same as those generated before
    assert results["Public key"] == public_key
    assert results["Proof of Possession"] == pop

def test_create_key_error_handling():
    # should fail with missing arguments (processSpringUtilCmd returning None)
    assert Utils.processSpringUtilCmd("bls create key", "missing arguments") == None

    # should fail when both arguments are present
    assert Utils.processSpringUtilCmd("bls create key --file out_file --to-console", "conflicting arguments") == None

def test_create_pop_error_handling():
    # should fail with missing arguments (processSpringUtilCmd returning None)
    assert Utils.processSpringUtilCmd("bls create pop", "missing arguments") == None

    # should fail when both arguments are present
    assert Utils.processSpringUtilCmd("bls create pop --file private_key_file --private-key", "conflicting arguments") == None

    # should fail when private key file does not exist
    with tempfile.NamedTemporaryFile(mode="w+") as temp_file:
        assert Utils.processSpringUtilCmd("bls create pop --file {}".format(temp_file.name), "private file not existing") == None

def check_create_key_results(rslts): 
    results = get_results(rslts)
    
    # check each output has valid value
    assert "PVT_BLS_" in results["Private key"]
    assert "PUB_BLS_" in results["Public key"]
    assert "SIG_BLS_" in results["Proof of Possession"]

def get_results(rslts):
    # sample output looks like
    # Private key: PVT_BLS_kRhJJ2MsM+/CddO...
    # Public key: PUB_BLS_lbUE8922wUfX0Iy5...
    # Proof of Possession: SIG_BLS_3jwkVUUYahHgsnmnEA...
    pattern = r'(\w+[^:]*): ([^\n]+)'
    matched= re.findall(pattern, rslts)
    
    results = {}
    for k, v in matched:
        results[k.strip()] = v.strip()

    return results

# tests start
try:
    # test create key to console 
    test_create_key_to_console()

    # test create key to file
    test_create_key_to_file()

    # test create pop from private key in command line
    test_create_pop_from_command_line()
    
    # test create pop from private key in file
    test_create_pop_from_file()

    # test error handling in create key
    test_create_key_error_handling()

    # test error handling in create pop
    test_create_pop_error_handling()

    testSuccessful=True
except Exception as e:
    Print(e)
    Utils.errorExit("exception during processing")

exitCode = 0 if testSuccessful else 1
exit(exitCode)
