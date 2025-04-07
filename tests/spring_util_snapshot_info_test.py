#!/usr/bin/env python3

import tempfile
import gzip
import shutil
import json

from TestHarness import Utils

Print=Utils.Print
testSuccessful=False

expected_results = [
    {
        "file": "unittests/snapshots/snap_v2.bin.gz",
        "result": {
            "version": 2,
            "chain_id": "90d8c7436017001e64891e8b6b7eb0060baeb350048eb52d2cdb594fdbc2617d",
            "head_block_id": "00000003ad205c959e2af857e2cc621d00a63c3df2b82d38392d5dd162b1c511",
            "head_block_num": 3,
            "head_block_time": "2020-01-01T00:00:01.000"
        }
    },
    {
        "file": "unittests/snapshots/snap_v3.bin.gz",
        "result": {
            "version": 3,
            "chain_id": "90d8c7436017001e64891e8b6b7eb0060baeb350048eb52d2cdb594fdbc2617d",
            "head_block_id": "00000003ad205c959e2af857e2cc621d00a63c3df2b82d38392d5dd162b1c511",
            "head_block_num": 3,
            "head_block_time": "2020-01-01T00:00:01.000"
        }
    },
    {
        "file": "unittests/snapshots/snap_v4.bin.gz",
        "result": {
            "version": 4,
            "chain_id": "90d8c7436017001e64891e8b6b7eb0060baeb350048eb52d2cdb594fdbc2617d",
            "head_block_id": "00000003ad205c959e2af857e2cc621d00a63c3df2b82d38392d5dd162b1c511",
            "head_block_num": 3,
            "head_block_time": "2020-01-01T00:00:01.000"
        }
    },
    {
        "file": "unittests/snapshots/snap_v5.bin.gz",
        "result": {
            "version": 5,
            "chain_id": "90d8c7436017001e64891e8b6b7eb0060baeb350048eb52d2cdb594fdbc2617d",
            "head_block_id": "00000003ad205c959e2af857e2cc621d00a63c3df2b82d38392d5dd162b1c511",
            "head_block_num": 3,
            "head_block_time": "2020-01-01T00:00:01.000"
        }
    },
    {
        "file": "unittests/snapshots/snap_v6.bin.gz",
        "result": {
            "version": 6,
            "chain_id": "90d8c7436017001e64891e8b6b7eb0060baeb350048eb52d2cdb594fdbc2617d",
            "head_block_id": "00000003ad205c959e2af857e2cc621d00a63c3df2b82d38392d5dd162b1c511",
            "head_block_num": 3,
            "head_block_time": "2020-01-01T00:00:01.000"
        }
    },
    {
        "file": "unittests/snapshots/snap_v8.bin.gz",
        "result": {
            "version": 8,
            "chain_id": "90d8c7436017001e64891e8b6b7eb0060baeb350048eb52d2cdb594fdbc2617d",
            "head_block_id": "00000003ad205c959e2af857e2cc621d00a63c3df2b82d38392d5dd162b1c511",
            "head_block_num": 3,
            "head_block_time": "2020-01-01T00:00:01.000"
        }
    }
]

def test_success():
    for test in expected_results:
        with gzip.open(test['file'], 'rb') as compressed_snap_file:
            with tempfile.NamedTemporaryFile('wb') as uncompressed_snap_file:
                shutil.copyfileobj(compressed_snap_file, uncompressed_snap_file)
                assert(test['result'] == json.loads(Utils.processSpringUtilCmd(f"snapshot info {uncompressed_snap_file.name}", "do snap info", silentErrors=False, exitOnError=True)))

def test_failure():
    assert(None == Utils.processSpringUtilCmd("snapshot info nonexistentfile.bin", "do snap info"))

try:
    test_success()
    test_failure()

    testSuccessful=True
except Exception as e:
    Print(e)
    Utils.errorExit("exception during processing")

exitCode = 0 if testSuccessful else 1
exit(exitCode)
