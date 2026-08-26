#!/usr/bin/env python3

import os
import re
import shutil
import signal
import subprocess
import tempfile
import time

from TestHarness import Cluster, TestHelper, Utils, WalletMgr

###############################################################################
# ship_log_util_test
#
# Exercises the "spring-util ship-log" utilities against state history logs
# produced by a real nodeos: info, smoke-test (--deep), make-index, repair
# (truncate and --keep-tail), trim, extract-blocks, split, and merge. Also
# verifies nodeos's state-history-force-write option regenerates a lying
# index and moves an unusable log aside rather than refusing to run.
#
###############################################################################

Print = Utils.Print

args = TestHelper.parse_args({"--dump-error-details", "--keep-logs", "-v", "--leave-running", "--unshared"})

Utils.Debug = args.v
cluster = Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
dumpErrorDetails = args.dump_error_details
walletPort = TestHelper.DEFAULT_WALLET_PORT

totalProducerNodes = 1
totalNonProducerNodes = 1  # for SHiP node
totalNodes = totalProducerNodes + totalNonProducerNodes

walletMgr = WalletMgr(True, port=walletPort)
testSuccessful = False

prodNodeId = 0
shipNodeId = 1

shipStride = 50

tmpDir = None


def shipLogUtil(subcommand, *extraArgs, expectSuccess=True):
    """Run "spring-util ship-log <subcommand> <extraArgs...>" and return (returncode, stdout+stderr)."""
    cmd = [Utils.SpringClientPath, "ship-log", subcommand] + [str(a) for a in extraArgs]
    if Utils.Debug:
        Utils.Print("cmd: %s" % (" ".join(cmd)))
    result = subprocess.run(cmd, capture_output=True, text=True)
    output = result.stdout + result.stderr
    if expectSuccess:
        assert result.returncode == 0, f"'{' '.join(cmd)}' failed ({result.returncode}):\n{output}"
    else:
        assert result.returncode != 0, f"'{' '.join(cmd)}' unexpectedly succeeded:\n{output}"
    return result.returncode, output


def parseInfoBlocks(infoOutput, logPath):
    """Extract the (first, last) block range the info subcommand reported for logPath."""
    stem = re.escape(os.path.splitext(logPath)[0])
    m = re.search(stem + r"\.log:.*?blocks:\s+(\d+)-(\d+)", infoOutput, re.DOTALL)
    assert m, f"no block range for {logPath} in:\n{infoOutput}"
    return int(m.group(1)), int(m.group(2))


def logRange(stemDir, logName):
    """Return the (first, last) block range of one bundle via the info subcommand."""
    _, out = shipLogUtil("info", "--state-history-dir", stemDir, "--log", logName)
    return parseInfoBlocks(out, os.path.join(stemDir, logName + ".log"))


def readIndexSlot(indexPath, slot):
    """Return the log position stored for the given zero-based slot of an index file."""
    with open(indexPath, "rb") as f:
        f.seek(slot * 8)
        return int.from_bytes(f.read(8), "little")


def copyBundle(srcStem, dstStem):
    shutil.copyfile(srcStem + ".log", dstStem + ".log")
    shutil.copyfile(srcStem + ".index", dstStem + ".index")


try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)
    Print("Stand up cluster")

    specificExtraNodeosArgs = {
        prodNodeId: "--plugin eosio::producer_api_plugin",
        shipNodeId: "--plugin eosio::state_history_plugin --trace-history --chain-state-history "
                    f"--finality-data-history --state-history-stride {shipStride} "
                    "--plugin eosio::net_api_plugin --plugin eosio::producer_api_plugin",
    }

    if cluster.launch(topo="mesh", pnodes=totalProducerNodes, totalNodes=totalNodes,
                      activateIF=True, specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up cluster.")

    cluster.waitOnClusterSync(blockAdvancing=5)
    Print("Cluster in Sync")

    Print("Shutdown unneeded bios node")
    cluster.biosNode.kill(signal.SIGTERM)

    prodNode = cluster.getNode(prodNodeId)
    shipNode = cluster.getNode(shipNodeId)

    # run past at least one stride boundary so a retained bundle exists
    assert Utils.waitForBool(lambda: prodNode.getHeadBlockNum() > shipStride + 10, timeout=300), \
        f"chain did not reach block {shipStride + 10}"

    Print("Pause producer and stop both nodes")
    prodNode.processUrllibRequest("producer", "pause", exitOnError=True)
    # pause can return while one more block is mid-production; wait for the head to settle so the
    # ids captured below cover every block the ship logs can end on
    headBlock = prodNode.getHeadBlockNum()
    for _ in range(60):
        time.sleep(1)
        newHead = prodNode.getHeadBlockNum()
        if newHead == headBlock:
            break
        headBlock = newHead
    else:
        Utils.errorExit(f"producer head did not settle after pause (still advancing at {headBlock})")
    shipNode.waitForBlock(headBlock)

    # capture chain block ids while the node is still up; the offline block-id checks below compare
    # what the ship logs recorded against these
    chainIds = {n: shipNode.getBlock(n)["id"] for n in range(max(2, headBlock - 3), headBlock + 1)}
    retainedProbeBlock = shipStride // 2
    retainedProbeId = shipNode.getBlock(retainedProbeBlock)["id"]

    prodNode.kill(signal.SIGTERM)
    shipNode.kill(signal.SIGTERM)

    shipDir = os.path.join(Utils.getNodeDataDir(shipNodeId), "state-history")
    retainedDir = os.path.join(shipDir, "retained")
    chainStateLog = os.path.join(shipDir, "chain_state_history.log")
    chainStateIndex = os.path.join(shipDir, "chain_state_history.index")
    traceIndex = os.path.join(shipDir, "trace_history.index")

    tmpDir = tempfile.mkdtemp()
    origChainStateLog = os.path.join(tmpDir, "chain_state_history.log")
    origChainStateIndex = os.path.join(tmpDir, "chain_state_history.index")
    origTraceIndex = os.path.join(tmpDir, "trace_history.index")

    Print("Save pristine SHiP files")
    shutil.copyfile(chainStateLog, origChainStateLog)
    shutil.copyfile(chainStateIndex, origChainStateIndex)
    shutil.copyfile(traceIndex, origTraceIndex)

    # -------- info + deep smoke-test on everything nodeos wrote
    Print("info and deep smoke-test on pristine logs")
    _, infoOut = shipLogUtil("info", "--state-history-dir", shipDir)
    headFirst, headLast = parseInfoBlocks(infoOut, chainStateLog)
    assert headLast >= headBlock - 1, f"chain_state log head {headLast} too far behind chain head {headBlock}"
    shipLogUtil("smoke-test", "--state-history-dir", shipDir, "--deep")

    retained = sorted(f for f in os.listdir(retainedDir) if re.match(r"chain_state_history-\d+-\d+\.log$", f))
    assert retained, f"expected retained bundles in {retainedDir}"
    shipLogUtil("smoke-test", "--log", os.path.join(retainedDir, retained[0]), "--deep")

    # -------- block-id reports the ids the logs actually recorded
    Print("block-id matches the chain")
    assert headLast in chainIds, \
        f"chain_state head log ends at {headLast}, outside the captured ids {sorted(chainIds)}"
    _, bidOut = shipLogUtil("block-id", "--state-history-dir", shipDir, "--block", headLast)
    assert bidOut.count(chainIds[headLast]) == 3, \
        f"expected all three head logs to record {chainIds[headLast]} for block {headLast}:\n{bidOut}"
    _, bidOut = shipLogUtil("block-id", "--log", os.path.join(retainedDir, retained[0]),
                            "--block", retainedProbeBlock)
    assert retainedProbeId in bidOut, \
        f"retained bundle did not record {retainedProbeId} for block {retainedProbeBlock}:\n{bidOut}"
    # block 1 predates every ship log; the lookup reports not-present and exits non-zero
    shipLogUtil("block-id", "--state-history-dir", shipDir, "--block", 1, expectSuccess=False)

    # -------- make-index reproduces the index nodeos wrote, byte for byte
    Print("make-index byte-equality test")
    os.remove(traceIndex)
    shipLogUtil("make-index", "--state-history-dir", shipDir, "--log", "trace_history")
    assert Utils.compareFiles(traceIndex, origTraceIndex, mode="rb"), "rebuilt trace index differs from nodeos's"

    # -------- repair a torn tail write (the classic crash case)
    Print("repair truncated-tail test")
    with open(chainStateLog, "ab") as f:
        f.write(b"\x5a" * 137)  # partial entry torn mid-write
    shipLogUtil("smoke-test", "--state-history-dir", shipDir, "--log", "chain_state_history", expectSuccess=False)
    # block-id must report the damage and exit non-zero, never crash, on a log the chain can no longer
    # open cleanly: returncode > 0 is a normal "damaged" exit; a negative returncode would be a signal
    # (segfault/abort), which is exactly the operator-facing failure this tooling exists to prevent
    rc, bidOut = shipLogUtil("block-id", "--state-history-dir", shipDir, "--log", "chain_state_history",
                             "--block", headLast, expectSuccess=False)
    assert rc > 0, f"block-id crashed (signal {-rc}) on a damaged log instead of reporting it:\n{bidOut}"
    assert "damaged" in bidOut.lower(), f"block-id did not report the damage on a corrupt log:\n{bidOut}"
    shipLogUtil("repair", "--state-history-dir", shipDir, "--log", "chain_state_history")
    assert Utils.compareFiles(chainStateLog, origChainStateLog, mode="rb"), "repair did not restore the exact log"
    assert Utils.compareFiles(chainStateIndex, origChainStateIndex, mode="rb"), "repair did not rebuild the index"

    Print("SHiP node relaunches on the repaired log")
    assert shipNode.relaunch(), "Failed to relaunch shipNode after repair"
    shipNode.kill(signal.SIGTERM)

    # -------- merge the retained bundles into one wide-range bundle for the remaining scenarios
    Print("merge retained bundles")
    mergeDir = os.path.join(tmpDir, "merged")
    shipLogUtil("merge", "--state-history-dir", retainedDir, "--log", "chain_state_history",
                "--output-dir", mergeDir)
    mergedStem = os.path.join(mergeDir, "chain_state_history")
    mergedFirst, mergedLast = logRange(mergeDir, "chain_state_history")
    assert mergedLast - mergedFirst >= shipStride - 5, f"merged range {mergedFirst}-{mergedLast} suspiciously narrow"
    shipLogUtil("smoke-test", "--state-history-dir", mergeDir, "--deep")

    # -------- trim a copy of the merged bundle down to a subrange
    Print("trim test")
    workDir = os.path.join(tmpDir, "work")
    os.makedirs(workDir)
    workStem = os.path.join(workDir, "chain_state_history")
    copyBundle(mergedStem, workStem)
    trimFirst, trimLast = mergedFirst + 10, mergedLast - 10
    shipLogUtil("trim", "--state-history-dir", workDir, "--log", "chain_state_history",
                "--first", trimFirst, "--last", trimLast)
    assert logRange(workDir, "chain_state_history") == (trimFirst, trimLast), "trim produced the wrong range"
    shipLogUtil("smoke-test", "--state-history-dir", workDir)

    # -------- extract a slice without touching the source
    Print("extract-blocks test")
    extractDir = os.path.join(tmpDir, "extracted")
    shipLogUtil("extract-blocks", "--state-history-dir", mergeDir, "--log", "chain_state_history",
                "--first", mergedFirst + 5, "--last", mergedFirst + 15, "--output-dir", extractDir)
    assert logRange(extractDir, "chain_state_history") == (mergedFirst + 5, mergedFirst + 15)
    shipLogUtil("smoke-test", "--state-history-dir", extractDir, "--deep")
    assert logRange(mergeDir, "chain_state_history") == (mergedFirst, mergedLast), "extract modified its source"

    # -------- keep-tail repair of mid-file damage
    Print("repair --keep-tail mid-file damage test")
    shutil.rmtree(workDir)
    os.makedirs(workDir)
    copyBundle(mergedStem, workStem)
    midBlock = (mergedFirst + mergedLast) // 2
    midPos = readIndexSlot(workStem + ".index", midBlock - mergedFirst)
    with open(workStem + ".log", "rb+") as f:
        f.seek(midPos)
        f.write(b"\x5a" * 16)  # destroy the mid entry's header
    # a dry run reports without touching anything
    _, dryOut = shipLogUtil("repair", "--state-history-dir", workDir, "--log", "chain_state_history",
                            "--keep-tail", "--dry-run")
    assert "DAMAGED" in dryOut, f"dry run did not report damage:\n{dryOut}"
    shipLogUtil("smoke-test", "--state-history-dir", workDir, "--log", "chain_state_history", expectSuccess=False)
    shipLogUtil("repair", "--state-history-dir", workDir, "--log", "chain_state_history", "--keep-tail")
    assert logRange(workDir, "chain_state_history") == (midBlock + 1, mergedLast), "keep-tail kept the wrong range"
    shipLogUtil("smoke-test", "--state-history-dir", workDir)

    # -------- split the merged bundle and merge it back
    Print("split and merge round-trip test")
    splitDir = os.path.join(tmpDir, "split")
    shipLogUtil("split", "--state-history-dir", mergeDir, "--log", "chain_state_history",
                "--stride", "20", "--output-dir", splitDir)
    splitBundles = sorted(f for f in os.listdir(splitDir) if f.endswith(".log"))
    assert len(splitBundles) >= 2, f"expected several split bundles, got {splitBundles}"
    shipLogUtil("smoke-test", "--state-history-dir", splitDir)

    remergeDir = os.path.join(tmpDir, "remerged")
    shipLogUtil("merge", "--state-history-dir", splitDir, "--log", "chain_state_history",
                "--output-dir", remergeDir)
    remergedFirst, remergedLast = logRange(remergeDir, "chain_state_history")
    assert remergedFirst == mergedFirst, f"re-merged range starts at {remergedFirst}, expected {mergedFirst}"
    shipLogUtil("smoke-test", "--state-history-dir", remergeDir, "--deep")

    # -------- state-history-force-write: an index that lies is regenerated instead of fatal
    Print("state-history-force-write index-disagreement test")
    with open(chainStateIndex, "rb+") as f:
        f.seek(-8, 2)
        f.write(b"\x00\x01\x02\x03\x04\x05\x06\x07")

    assert not shipNode.relaunch(), "SHiP node should refuse to start with a lying index"
    assert shipNode.relaunch(chainArg="--state-history-force-write"), \
        "SHiP node should start with state-history-force-write"
    shipNode.kill(signal.SIGTERM)
    assert Utils.compareFiles(chainStateIndex, origChainStateIndex, mode="rb"), \
        "force-write should have regenerated the index"
    assert not [f for f in os.listdir(shipDir) if "-corrupt-" in f], \
        "an index disagreement must not orphan the log"

    # -------- state-history-force-write: a log that cannot accept the next block is moved aside
    Print("state-history-force-write head-gap test")
    # replace the chain_state head log with one ending far behind the chain's head and clear its
    # retained bundles; the next block nodeos writes is then a gap the log cannot represent
    gapLast = mergedFirst + 20
    gapDir = os.path.join(tmpDir, "gap")
    os.makedirs(gapDir)
    copyBundle(mergedStem, os.path.join(gapDir, "chain_state_history"))
    shipLogUtil("trim", "--state-history-dir", gapDir, "--log", "chain_state_history", "--last", gapLast)
    copyBundle(os.path.join(gapDir, "chain_state_history"), os.path.join(shipDir, "chain_state_history"))
    for f in os.listdir(retainedDir):
        if f.startswith("chain_state_history-"):
            os.remove(os.path.join(retainedDir, f))

    assert prodNode.relaunch(chainArg="--enable-stale-production"), "Failed to relaunch prodNode"
    # relaunch(chainArg=...) is sticky: the force-write flag from the previous relaunch is still on
    # the command line, and passing it again would duplicate the switch
    assert shipNode.relaunch(), "SHiP node should start with state-history-force-write"
    assert shipNode.waitForHeadToAdvance(), "Head did not advance on shipNode"

    def orphanAppeared():
        return bool([f for f in os.listdir(shipDir) if re.match(r"chain_state_history-corrupt-\d+\.log$", f)])
    assert Utils.waitForBool(orphanAppeared, timeout=60), "force-write did not move the gapped log aside"

    orphanFirst, orphanLast = logRange(shipDir, "chain_state_history-corrupt-1")
    assert orphanLast == gapLast, f"orphan should hold the gapped log ending at {gapLast}, ends at {orphanLast}"

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)
    if tmpDir is not None:
        shutil.rmtree(tmpDir, ignore_errors=True)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
