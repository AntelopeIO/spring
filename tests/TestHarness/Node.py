import copy
import decimal
import subprocess
import time
import os
import re
import json
import shlex
import signal
import sys
import shutil
from pathlib import Path
from typing import List
from dataclasses import InitVar, dataclass, field, is_dataclass, asdict

from datetime import datetime
from datetime import timedelta
from .core_symbol import CORE_SYMBOL
from .queries import NodeosQueries, BlockType
from .transactions import Transactions
from .accounts import Account
from .testUtils import Utils
from .testUtils import unhandledEnumType
from .testUtils import ReturnType

@dataclass
class KeyStrings(object):
    pubkey: str
    privkey: str
    blspubkey: str = None
    blsprivkey: str = None
    blspop: str = None

# pylint: disable=too-many-public-methods
class Node(Transactions):
    # Node number is used as an addend to determine the node listen ports.
    # This value extends that pattern to all nodes, not just the numbered nodes.
    biosNodeId = -100

    # pylint: disable=too-many-instance-attributes
    # pylint: disable=too-many-arguments
    def __init__(self, host, port, nodeId: int, data_dir: Path, config_dir: Path, cmd: List[str], unstarted=False, launch_time=None, walletMgr=None, nodeosVers=""):
        super().__init__(host, port, walletMgr)
        assert isinstance(data_dir, Path), 'data_dir must be a Path instance'
        assert isinstance(config_dir, Path), 'config_dir must be a Path instance'
        assert isinstance(cmd, list), 'cmd must be a list'
        self.host=host
        self.port=port
        self.cmd=cmd
        if nodeId == Node.biosNodeId:
            self.nodeId='bios'
            self.name='node_bios'
        else:
            self.nodeId=nodeId
            self.name=f'node_{str(nodeId).zfill(2)}'
        if not unstarted:
            self.popenProc=self.launchCmd(self.cmd, data_dir, launch_time)
            self.pid=self.popenProc.pid
        else:
            self.popenProc=None
            self.pid=None
            if Utils.Debug: Utils.Print(f'unstarted node command: {" ".join(self.cmd)}')
        start = data_dir / 'start.cmd'
        with start.open('w') as f:
            f.write(' '.join(cmd))
        self.killed=False
        self.infoValid=None
        self.lastRetrievedHeadBlockNum=None
        self.lastRetrievedLIB=None
        self.lastRetrievedHeadBlockProducer=""
        self.transCache={}
        self.missingTransaction=False
        self.lastTrackedTransactionId=None
        self.nodeosVers=nodeosVers
        self.data_dir=data_dir
        self.config_dir=config_dir
        self.launch_time=launch_time
        self.isProducer=False
        # if multiple producers configured for a Node, this is the first one
        self.producerName=None
        self.keys: List[KeyStrings] = field(default_factory=list)

    def __str__(self):
        return "Host: %s, Port:%d, NodeNum:%s, Pid:%s" % (self.host, self.port, self.nodeId, self.pid)

    @staticmethod
    def __printTransStructureError(trans, context):
        Utils.Print("ERROR: Failure in expected transaction structure. Missing trans%s." % (context))
        Utils.Print("Transaction: %s" % (json.dumps(trans, indent=1)))

    @staticmethod
    def stdinAndCheckOutput(cmd, subcommand):
        """Passes input to stdin, executes cmd. Returns tuple with return code(int), stdout(byte stream) and stderr(byte stream)."""
        assert(cmd)
        assert(isinstance(cmd, list))
        assert(subcommand)
        assert(isinstance(subcommand, str))
        outs=None
        errs=None
        ret=0
        try:
            popen=subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            outs,errs=popen.communicate(input=subcommand.encode("utf-8"))
            ret=popen.wait()
        except subprocess.CalledProcessError as ex:
            msg=ex.stderr
            return (ex.returncode, msg, None)

        return (ret, outs, errs)

    @staticmethod
    def normalizeJsonObject(extJStr):
        tmpStr=extJStr
        tmpStr=re.sub(r'ObjectId\("(\w+)"\)', r'"ObjectId-\1"', tmpStr)
        tmpStr=re.sub(r'ISODate\("([\w|\-|\:|\.]+)"\)', r'"ISODate-\1"', tmpStr)
        tmpStr=re.sub(r'NumberLong\("(\w+)"\)', r'"NumberLong-\1"', tmpStr)
        tmpStr=re.sub(r'NumberLong\((\w+)\)', r'\1', tmpStr)
        return tmpStr

    @staticmethod
    def byteArrToStr(arr):
        return arr.decode("utf-8")

    def validateAccounts(self, accounts):
        assert(accounts)
        assert(isinstance(accounts, list))

        for account in accounts:
            assert(account)
            assert(isinstance(account, Account))
            if Utils.Debug: Utils.Print("Validating account %s" % (account.name))
            accountInfo=self.getEosAccount(account.name, exitOnError=True)
            try:
                assert(accountInfo["account_name"] == account.name)
            except (AssertionError, TypeError, KeyError) as _:
                Utils.Print("account validation failed. account: %s" % (account.name))
                raise

    def waitForTransactionInBlock(self, transId, timeout=None, exitOnError=False):
        """Wait for trans id to appear in a block."""
        assert(isinstance(transId, str))
        lam = lambda: self.isTransInAnyBlock(transId, exitOnError=exitOnError)
        ret=Utils.waitForBool(lam, timeout)
        return ret

    def checkBlockForTransactions(self, transIds, blockNum):
        block = self.processUrllibRequest("trace_api", "get_block", {"block_num":blockNum}, silentErrors=False, exitOnError=True)
        if block['payload']['transactions']:
            for trx in block['payload']['transactions']:
                if trx['id'] in transIds:
                    transIds.pop(trx['id'])
        return transIds

    def waitForTransactionsInBlockRange(self, transIds, startBlock, endBlock):
        nextBlockToProcess = startBlock
        while len(transIds) > 0:
            currentLoopEndBlock = self.getHeadBlockNum()
            if currentLoopEndBlock > endBlock:
                currentLoopEndBlock = endBlock
            for blockNum in range(nextBlockToProcess, currentLoopEndBlock + 1):
                transIds = self.checkBlockForTransactions(transIds, blockNum)
                if len(transIds) == 0:
                    return transIds
            nextBlockToProcess = currentLoopEndBlock + 1
            if currentLoopEndBlock == endBlock:
                Utils.Print("ERROR: Transactions were missing upon expiration of waitOnblockTransactions")
                break
            self.waitForHeadToAdvance()
        return transIds

    def waitForTransactionsInBlock(self, transIds, timeout=None):
        status = True
        for transId in transIds:
            status &= self.waitForTransactionInBlock(transId, timeout)
        return status

    def waitForTransFinalization(self, transId, timeout=None):
        """Wait for trans id to be finalized."""
        assert(isinstance(transId, str))
        lam = lambda: self.isTransFinalized(transId)
        ret=Utils.waitForBool(lam, timeout)
        return ret

    def waitForNextBlock(self, timeout=None, blockType=BlockType.head):
        num=self.getBlockNum(blockType=blockType)
        lam = lambda: self.getBlockNum(blockType=blockType) > num
        ret=Utils.waitForBool(lam, timeout)
        return ret

    def waitForBlock(self, blockNum, timeout=None, blockType=BlockType.head, reportInterval=None):
        lam = lambda: self.getBlockNum(blockType=blockType) >= blockNum
        blockDesc = "head" if blockType == BlockType.head else "LIB"
        count = 0

        class WaitReporter:
            def __init__(self, node, reportInterval):
                self.count = 0
                self.node = node
                self.reportInterval = reportInterval

            def __call__(self):
                self.count += 1
                if self.count % self.reportInterval == 0:
                    info = self.node.getInfo()
                    Utils.Print("Waiting on %s block num %d, get info = {\n%s\n}" % (blockDesc, blockNum, info))

        reporter = WaitReporter(self, reportInterval) if reportInterval is not None else None
        ret=Utils.waitForBool(lam, timeout, reporter=reporter)
        return ret

    def waitForIrreversibleBlock(self, blockNum, timeout=None, reportInterval=None):
        return self.waitForBlock(blockNum, timeout=timeout, blockType=BlockType.lib, reportInterval=reportInterval)

    def waitForTransBlockIfNeeded(self, trans, waitForTransBlock, exitOnError=False):
        if not waitForTransBlock:
            return trans
        transId=NodeosQueries.getTransId(trans)
        if not self.waitForTransactionInBlock(transId, exitOnError=exitOnError):
            if exitOnError:
                Utils.cmdError("transaction with id %s never made it into a block" % (transId))
                Utils.errorExit("Failed to find transaction with id %s in a block before timeout" % (transId))
            return None
        return trans

    def waitForHeadToAdvance(self, blocksToAdvance=1, timeout=None):
        currentHead = self.getHeadBlockNum()
        if timeout is None:
            timeout = 6 + blocksToAdvance / 2
        def isHeadAdvancing():
            return self.getHeadBlockNum() >= currentHead + blocksToAdvance
        return Utils.waitForBool(isHeadAdvancing, timeout, sleepTime=0.5)

    def waitForLibToAdvance(self, timeout=30):
        currentLib = self.getIrreversibleBlockNum()
        def isLibAdvancing():
            return self.getIrreversibleBlockNum() > currentLib
        return Utils.waitForBool(isLibAdvancing, timeout)

    def waitForLibNotToAdvance(self, timeout=30):
        endTime=time.time()+timeout
        while self.waitForLibToAdvance(timeout=timeout):
            if time.time() > endTime:
                return False
        return True

    def waitForAnyProducer(self, producers, timeout=None, exitOnError=False):
        if timeout is None:
            # default to the typical configuration of 21 producers, each producing 12 blocks in a row (every 1/2 second)
            timeout = 21 * 6
        start=time.perf_counter()
        Utils.Print(self.getInfo())
        initialProducer=self.getInfo()["head_block_producer"]
        def isProducerInList():
            return self.getInfo()["head_block_producer"] in producers
        found = Utils.waitForBool(isProducerInList, timeout)
        assert not exitOnError or found, \
            f"Waited for {time.perf_counter()-start} sec but never found a producer in: {producers}. Started with {initialProducer} and ended with {self.getInfo()['head_block_producer']}"
        return found

    def waitForProducer(self, producer, timeout=None, exitOnError=False):
        return self.waitForAnyProducer([producer], timeout, exitOnError)

    # returns True if the node has missed next scheduled production round.
    def missedNextProductionRound(self):
        # Cannot use producer_plugin's paused() endpoint as it does not
        # include paused due to max-reversible-blocks exceeded.
        # The idea is to find the scheduled start block of node's producer's
        # next round. If that block is not produced, it means block production
        # on the node is paused.

        assert self.isProducer, 'missedNextProductionRound can be only called on a producer'

        blocksPerProducer = 12

        scheduled_producers = []
        schedule = self.processUrllibRequest("chain", "get_producer_schedule")
        for prod in schedule["payload"]["active"]["producers"]:
            scheduled_producers.append(prod["producer_name"])
        if Utils.Debug: Utils.Print(f'scheduled_producers {scheduled_producers}')

        self.getInfo()
        currBlockNum=self.lastRetrievedHeadBlockNum
        currProducer=self.lastRetrievedHeadBlockProducer
        blocksRemainedInCurrRound = blocksPerProducer - currBlockNum % blocksPerProducer - 1
        if Utils.Debug: Utils.Print(f'currBlockNum {currBlockNum}, currProducer {currProducer}, blocksRemainedInCurrRound {blocksRemainedInCurrRound}')

        # find the positions of currProducerPos and nodeProducer in the schedule
        currProducerPos=0
        nodeProducerPos=0
        for i in range(0, len(scheduled_producers)):
            if scheduled_producers[i] == currProducer:
                currProducerPos=i
            if scheduled_producers[i] == self.producerName:
                nodeProducerPos=i

        # find the number of the blocks to node producer's next scheduled round
        blocksToNextScheduledRound = 0
        if currProducerPos < nodeProducerPos:
            # nodeProducerPos - currProducerPos - 1 is the number of producers
            # from current producer to the node producer in the schedule
            blocksToNextScheduledRound = (nodeProducerPos - currProducerPos - 1) * blocksPerProducer + blocksRemainedInCurrRound + 1
        else:
            # nodeProducerPos is the number of producers before node producer in the schedule
            # len(scheduled_producers) - currProducerPos - 1 is the number
            # of producers after node producer in the schedule
            blocksToNextScheduledRound = (nodeProducerPos + (len(scheduled_producers)  - currProducerPos - 1)) * blocksPerProducer + blocksRemainedInCurrRound + 1

        # find the block number of the node producer's next scheduled round
        nextScheduledRoundBlockNum=currBlockNum + blocksToNextScheduledRound
        timeout=blocksToNextScheduledRound/2 + 2 # leave 2 seconds for avoid flakiness
        if Utils.Debug: Utils.Print(f'blocksToNextScheduledRound {blocksToNextScheduledRound}, nextScheduledRoundBlockNum {nextScheduledRoundBlockNum}, timeout {timeout}')

        return not self.waitForBlock(nextScheduledRoundBlockNum, timeout=timeout)

    def killNodeOnProducer(self, producer, whereInSequence, blockType=BlockType.head, silentErrors=True, exitOnError=False, exitMsg=None, returnType=ReturnType.json):
        assert(isinstance(producer, str))
        assert(isinstance(whereInSequence, int))
        assert(isinstance(blockType, BlockType))
        assert(isinstance(returnType, ReturnType))
        basedOnLib="true" if blockType==BlockType.lib else "false"
        payload={ "producer":producer, "where_in_sequence":whereInSequence, "based_on_lib":basedOnLib }
        return self.processUrllibRequest("test_control", "kill_node_on_producer", payload, silentErrors=silentErrors, exitOnError=exitOnError, exitMsg=exitMsg, returnType=returnType)

    def kill(self, killSignal):
        if Utils.Debug: Utils.Print("Killing node: %s" % (self.cmd))
        try:
            if self.popenProc is not None:
                self.popenProc.send_signal(killSignal)
                self.popenProc.wait()
            elif self.pid is not None:
                os.kill(self.pid, killSignal)

                # wait for kill validation
                def myFunc():
                    try:
                        os.kill(self.pid, 0) #check if process with pid is running
                    except OSError as _:
                        return True
                    return False

                if not Utils.waitForBool(myFunc):
                    Utils.Print("ERROR: Failed to validate node shutdown.")
                    return False
            else:
                if Utils.Debug: Utils.Print(f"Called kill on node {self.nodeId} but it has already exited.")
        except OSError as ex:
            Utils.Print("ERROR: Failed to kill node (%s)." % (self.cmd), ex)
            return False

        # mark node as killed
        self.pid=None
        self.killed=True
        return True

    def interruptAndVerifyExitStatus(self, timeout=60):
        if Utils.Debug: Utils.Print("terminating node: %s" % (self.cmd))
        assert self.popenProc is not None, f"node: '{self.cmd}' does not have a popenProc."
        self.popenProc.send_signal(signal.SIGINT)
        try:
            outs, _ = self.popenProc.communicate(timeout=timeout)
            assert self.popenProc.returncode == 0, f"Expected terminating '{self.cmd}' to have an exit status of 0, but got {self.popenProc.returncode}"
        except subprocess.TimeoutExpired:
            Utils.errorExit("Terminate call failed on node: %s" % (self.cmd))

        # mark node as killed
        self.pid=None
        self.killed=True

    def verifyAlive(self, silent=False):
        logStatus=not silent and Utils.Debug
        pid=self.pid
        if logStatus: Utils.Print(f'Checking if node id {self.nodeId} (pid={self.pid}) is alive (killed={self.killed}): {self.cmd}')
        if self.killed or self.pid is None or self.popenProc is None:
            self.killed=True
            self.pid=None
            return False

        if self.popenProc.poll() is not None:
            self.pid=None
            self.killed=True
            if logStatus: Utils.Print(f'Determined node id {self.nodeId} (formerly pid={pid}) is killed')
            return False
        else:
            if logStatus: Utils.Print(f'Determined node id {self.nodeId} (pid={pid}) is alive')
            return True

    def rmFromCmd(self, matchValue: str) -> str:
        '''Removes all instances of matchValue from cmd array and succeeding value if it's an option value string.
           Returns the removed strings as a space-delimited string.'''
        if not self.cmd:
            return ''

        removed_items = []

        while True:
            try:
                i = self.cmd.index(matchValue)
                removed_items.append(self.cmd.pop(i))  # Store the matchValue
                if len(self.cmd) > i:
                    if self.cmd[i][0] != '-':  # Check if the next value isn't an option (doesn't start with '-')
                        removed_items.append(self.cmd.pop(i))  # Store the succeeding value
            except ValueError:
                break

        return ' '.join(removed_items)  # Return the removed strings as a space-delimited string

    def waitForNodeToExit(self, timeout):
        def didNodeExitGracefully(popen, timeout):
            try:
                popen.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                return False
            with open(popen.errfile.name, 'r') as f:
                if "successfully exiting" in f.read():
                    return True
                else:
                    return False

        return Utils.waitForBoolWithArg(didNodeExitGracefully, self.popenProc, timeout, sleepTime=1)

    # pylint: disable=too-many-locals
    # If nodeosPath is equal to None, it will use the existing nodeos path
    def relaunch(self, chainArg=None, newChain=False, skipGenesis=True, timeout=Utils.systemWaitTimeout,
                 addSwapFlags=None, rmArgs=None, nodeosPath=None, waitForTerm=False):

        assert(self.pid is None)
        assert(self.killed)

        if Utils.Debug: Utils.Print(f"Launching node process, Id: {self.nodeId}")

        cmdArr=self.cmd[:]
        if nodeosPath: cmdArr[0] = nodeosPath
        toAddOrSwap=copy.deepcopy(addSwapFlags) if addSwapFlags is not None else {}
        if rmArgs is not None:
            for v in shlex.split(rmArgs):
                i = cmdArr.index(v)
                cmdArr.pop(i)
        if not newChain:
            if skipGenesis:
                try:
                    i = cmdArr.index('--genesis-json')
                    cmdArr.pop(i)
                    cmdArr.pop(i)
                    i = cmdArr.index('--genesis-timestamp')
                    cmdArr.pop(i)
                    cmdArr.pop(i)
                except ValueError:
                    pass
            for k,v in toAddOrSwap.items():
                try:
                    i = cmdArr.index(k)
                    cmdArr[i+1] = v
                except ValueError:
                    cmdArr.append(k)
                    if v:
                        cmdArr.append(v)

        if chainArg:
            cmdArr.extend(shlex.split(chainArg))
        self.popenProc=self.launchCmd(cmdArr, self.data_dir, launch_time=datetime.now().strftime('%Y_%m_%d_%H_%M_%S'))
        self.pid=self.popenProc.pid

        def isNodeAlive():
            """wait for node to be responsive."""
            try:
                return True if self.checkPulse() else False
            except (TypeError) as _:
                pass
            return False

        if waitForTerm:
            isAlive=self.waitForNodeToExit(timeout)
        else:
            isAlive=Utils.waitForBool(isNodeAlive, timeout, sleepTime=1)

        if isAlive:
            Utils.Print("Node relaunch was successful.")
        else:
            Utils.Print("ERROR: Node relaunch Failed.")
            # Ensure the node process is really killed
            if self.popenProc:
                self.popenProc.send_signal(signal.SIGTERM)
                self.popenProc.wait()
            self.pid=None
            return False

        self.cmd=cmdArr
        self.killed=False
        return True

    @staticmethod
    def unstartedFile(nodeId):
        assert(isinstance(nodeId, int))
        startFile=Utils.getNodeDataDir(nodeId, "start.cmd")
        if not os.path.exists(startFile):
            Utils.errorExit("Cannot find unstarted node since %s file does not exist" % startFile)
        return startFile

    def launchUnstarted(self, waitForAlive=True):
        Utils.Print("launchUnstarted cmd: %s" % (self.cmd))
        self.popenProc = self.launchCmd(self.cmd, self.data_dir, self.launch_time)

        if not waitForAlive:
            return

        def isNodeAlive():
            """wait for node to be responsive."""
            try:
                return True if self.checkPulse() else False
            except (TypeError) as _:
                pass
            return False

        isAlive=Utils.waitForBool(isNodeAlive)

        if isAlive:
            if Utils.Debug: Utils.Print("Node launch was successful.")
        else:
            Utils.Print("ERROR: Node launch Failed.")
            # Ensure the node process is really killed
            if self.popenProc:
                self.popenProc.send_signal(signal.SIGTERM)
                self.popenProc.wait()
            self.pid=None

    def launchCmd(self, cmd: List[str], data_dir: Path, launch_time: str):
        dd = data_dir
        out = dd / 'stdout.txt'
        err_sl = dd / 'stderr.txt'
        err = dd / Path(f'stderr.{launch_time}.txt')
        pidf = dd / Path(f'{Utils.EosServerName}.pid')

        # make sure unique file name to avoid overwrite of existing log file
        i = 0
        while err.is_file():
            i = i + 1
            err = dd / Path(f'stderr.{launch_time}-{i}.txt')

        Utils.Print(f'spawning child: {" ".join(cmd)}')
        dd.mkdir(parents=True, exist_ok=True)
        with out.open('w') as sout, err.open('w') as serr:
            popen = subprocess.Popen(cmd, stdout=sout, stderr=serr)
            popen.outfile = sout
            popen.errfile = serr
            self.pid = popen.pid
            self.cmd = cmd
            self.isProducer = '--producer-name' in self.cmd
            # first configured producer or None
            self.producerName = re.search(r'--producer-name (\w+)', " ".join(cmd))[1] if re.search(r'--producer-name (\w+)', " ".join(cmd)) is not None else None
        with pidf.open('w') as pidout:
            pidout.write(str(popen.pid))
        try:
            err_sl.unlink()
        except FileNotFoundError:
            pass
        err_sl.symlink_to(err.name)
        return popen

    def trackCmdTransaction(self, trans, ignoreNonTrans=False, reportStatus=True):
        if trans is None:
            if Utils.Debug: Utils.Print("  cmd returned transaction: %s" % (trans))
            return

        if ignoreNonTrans and not NodeosQueries.isTrans(trans):
            if Utils.Debug: Utils.Print("  cmd returned a non-transaction: %s" % (trans))
            return

        transId=NodeosQueries.getTransId(trans)
        self.lastTrackedTransactionId=transId
        if transId in self.transCache.keys():
            replaceMsg="replacing previous trans=\n%s" % json.dumps(self.transCache[transId], indent=2, sort_keys=True)
        else:
            replaceMsg=""

        if Utils.Debug and reportStatus:
            status=NodeosQueries.getTransStatus(trans)
            blockNum=NodeosQueries.getTransBlockNum(trans)
            Utils.Print("  cmd returned transaction id: %s, status: %s, (possible) block num: %s %s" % (transId, status, blockNum, replaceMsg))
        elif Utils.Debug:
            Utils.Print("  cmd returned transaction id: %s %s" % (transId, replaceMsg))

        self.transCache[transId]=trans

    def getLastTrackedTransactionId(self):
        return self.lastTrackedTransactionId

    def reportStatus(self):
        Utils.Print("Node State:")
        Utils.Print(" cmd   : %s" % (self.cmd))
        self.verifyAlive(silent=True)
        Utils.Print(" killed: %s" % (self.killed))
        Utils.Print(" host  : %s" % (self.host))
        Utils.Print(" port  : %s" % (self.port))
        Utils.Print(" pid   : %s" % (self.pid))
        status="last getInfo returned None" if not self.infoValid else "at last call to getInfo"
        Utils.Print(" hbn   : %s (%s)" % (self.lastRetrievedHeadBlockNum, status))
        Utils.Print(" lib   : %s (%s)" % (self.lastRetrievedLIB, status))

    # Require producer_api_plugin
    def scheduleProtocolFeatureActivations(self, featureDigests=[]):
        param = { "protocol_features_to_activate": featureDigests }
        self.processUrllibRequest("producer", "schedule_protocol_feature_activations", param)

    def modifyBuiltinPFSubjRestrictions(self, featureCodename, subjectiveRestriction={}):
        jsonPath = os.path.join(self.config_dir,
                                "protocol_features",
                                "BUILTIN-{}.json".format(featureCodename))
        protocolFeatureJson = []
        with open(jsonPath) as f:
            protocolFeatureJson = json.load(f)
        protocolFeatureJson["subjective_restrictions"].update(subjectiveRestriction)
        with open(jsonPath, "w") as f:
            json.dump(protocolFeatureJson, f, indent=2)

    def getFinalizerInfo(self):
       return self.processUrllibRequest("chain", "get_finalizer_info",silentErrors=False, exitOnError=True)

    # Require producer_api_plugin
    def createSnapshot(self):
        return self.processUrllibRequest("producer", "create_snapshot")

    def scheduleSnapshot(self):
        return self.processUrllibRequest("producer", "schedule_snapshot")
    
    def scheduleSnapshotAt(self, sbn):
        param = { "start_block_num": sbn, "end_block_num": sbn }
        return self.processUrllibRequest("producer", "schedule_snapshot", param)

    def getLatestSnapshot(self):
       snapshotDir = os.path.join(Utils.getNodeDataDir(self.nodeId), "snapshots")
       snapshotDirContents = os.listdir(snapshotDir)
       assert len(snapshotDirContents) > 0
       # disregard snapshot schedule config in same folder
       snapshotScheduleDB = "snapshot-schedule.json"
       if snapshotScheduleDB in snapshotDirContents: snapshotDirContents.remove(snapshotScheduleDB)
       snapshotDirContents.sort()
       return os.path.join(snapshotDir, snapshotDirContents[-1])

    def removeDataDir(self, rmState=True, rmBlocks=True, rmStateHist=True, rmFinalizersSafetyDir=True):
        if rmState:
            shutil.rmtree(Utils.getNodeDataDir(self.nodeId, "state"))
        if rmBlocks:
            shutil.rmtree(Utils.getNodeDataDir(self.nodeId, "blocks"))
        if rmStateHist:
            shutil.rmtree(Utils.getNodeDataDir(self.nodeId, "state-history"), ignore_errors=True)
        if rmFinalizersSafetyDir:
            shutil.rmtree(Utils.getNodeDataDir(self.nodeId, "finalizers"), ignore_errors=True)

    def removeState(self):
       dataDir = Utils.getNodeDataDir(self.nodeId)
       state = os.path.join(dataDir, "state")
       shutil.rmtree(state, ignore_errors=True)

    def removeReversibleBlks(self):
        dataDir = Utils.getNodeDataDir(self.nodeId)
        reversibleBlks = os.path.join(dataDir, "blocks", "reversible")
        shutil.rmtree(reversibleBlks, ignore_errors=True)

    def removeFinalizersSafetyDir(self):
        dataDir = Utils.getNodeDataDir(self.nodeId)
        finalizersDir = os.path.join(dataDir, "finalizers")
        shutil.rmtree(finalizersDir, ignore_errors=True)

    def removeTracesDir(self):
        dataDir = Utils.getNodeDataDir(self.nodeId)
        tracesDir = os.path.join(dataDir, "traces")
        shutil.rmtree(tracesDir, ignore_errors=True)

    @staticmethod
    def findStderrFiles(path):
        files=[]
        it=os.scandir(path)
        for entry in it:
            if entry.is_file(follow_symlinks=False):
                match=re.match(r"stderr\..+\.txt", entry.name)
                if match:
                    files.append(os.path.join(path, entry.name))
        files.sort()
        return files

    def findInLog(self, searchStr):
        dataDir=Utils.getNodeDataDir(self.nodeId)
        files=Node.findStderrFiles(dataDir)
        pattern = re.compile(searchStr)
        for file in files:
            with open(file, 'r') as f:
                for line in f:
                    if pattern.search(line):
                        return True
        return False

    def linesInLog(self, searchStr):
        dataDir=Utils.getNodeDataDir(self.nodeId)
        files=Node.findStderrFiles(dataDir)
        pattern = re.compile(searchStr)
        lines=[]
        for file in files:
            with open(file, 'r') as f:
                for line in f:
                    if pattern.search(line):
                        lines.append(line)
        return lines

    # Verfify that in during synching, unlinkable blocks are expected if
    # the number of each group of consecutive unlinkable blocks is less than sync fetch span
    def verifyUnlinkableBlocksExpected(self, syncFetchSpan) -> bool:
        dataDir=Utils.getNodeDataDir(self.nodeId)
        files=Node.findStderrFiles(dataDir)

        # A sample of unique line of unlinkable_block in logging file looks like:
        # debug 2024-11-06T16:28:21.216 net-0 net_plugin.cpp:3744 operator() unlinkable_block 144 : 0000009063379d966646fede5662c76c970dd53ea3a3a38d4311625b72971b07, previous 143 : 0000008f172a24dd573825702ff7bdeec92ea6c2c3b22a5303a27cc367ee5a52
        pattern = re.compile(r"unlinkable_block\s(\d+)")

        for file in files:
            blocks = []
            with open(file, 'r') as f:
                for line in f:
                    match = pattern.search(line)
                    if match:
                        try:
                            blockNum = int(match.group(1))
                            blocks.append(blockNum)
                        except ValueError:
                            Utils.Print(f"unlinkable block number cannot be converted into integer: in {line.strip()} of {f}")
                            return False
                blocks.sort() # blocks from multiple connections might be out of order
                Utils.Print(f"Unlinkable blocks: {blocks}")
                numConsecutiveUnlinkableBlocks = 0 if len(blocks) == 0 else 1 # numConsecutiveUnlinkableBlocks is at least 1 if len(blocks) > 0
                for i in range(1, len(blocks)):
                    if blocks[i] == blocks[i - 1] or blocks[i] == blocks[i - 1] + 1: # look for consecutive blocks, including duplicate
                        if blocks[i] == blocks[i - 1] + 1: # excluding duplicate
                            ++numConsecutiveUnlinkableBlocks
                    else: # start a new group of consecutive blocks
                        if numConsecutiveUnlinkableBlocks > syncFetchSpan:
                            Utils.Print(f"the number of a group of unlinkable blocks {numConsecutiveUnlinkableBlocks} greater than syncFetchSpan {syncFetchSpan} in {f}")
                            return False
                        numConsecutiveUnlinkableBlocks = 1
        if numConsecutiveUnlinkableBlocks > syncFetchSpan:
            Utils.Print(f"the number of a group of unlinkable blocks {numConsecutiveUnlinkableBlocks} greater than syncFetchSpan {syncFetchSpan} in {f}")
            return False
        else:
            return True

    # Returns the number of unique unlinkable blocks in stderr.txt.
    def numUniqueUnlinkableBlocks(self) -> int:
        dataDir = Utils.getNodeDataDir(self.nodeId)
        logFile = dataDir + "/stderr.txt"

        pattern = re.compile(r"unlinkable_block\s(\d+)")

        # Use set for uniqueness, as the same block can be unlinkable multiple
        # times due to multiple connections.
        uniqueBlocks = set()
        with open(logFile, 'r') as f:
            for line in f:
                match = pattern.search(line)
                if match:
                    try:
                        blockNum = int(match.group(1))
                        uniqueBlocks.add(blockNum)
                    except ValueError:
                        Utils.Print(f"unlinkable block number cannot be converted into integer: in {line.strip()} of {f}")
                        assert(False)  # Cannot happen. Fail the test.
        numUnlinkableBlocks = len(uniqueBlocks)
        Utils.Print(f"Number of unique unlinkable blocks: {numUnlinkableBlocks}")
        return numUnlinkableBlocks

    # Verify that we have only one "Starting block" in the log for any block number unless:
    # - the block was restarted because it was exhausted,
    # - or the second "Starting block" is for a different block time than the first.
    # -------------------------------------------------------------------------------------
    def verifyStartingBlockMessages(self):
        dataDir=Utils.getNodeDataDir(self.nodeId)
        files=Node.findStderrFiles(dataDir)
        restarting_exhausted_regexp = re.compile(r"Restarting exhausted speculative block #(\d+)")
        starting_block_regexp       = re.compile(r"Starting block #(\d+) .*(\d\d:\d\d\.\d\d\d) producer")

        for f in files:
            notRestartedBlockNumbersAndTimes = {}
            duplicateStartFound = False

            with open(f, 'r') as file:
                for line in file:
                    match = restarting_exhausted_regexp.match(line)
                    if match:
                        # remove restarted block
                        notRestartedBlockNumbersAndTimes.pop(match.group(1), None)
                        continue
                    match = starting_block_regexp.match(line)
                    if match:
                        blockNumber, time = match.group(1), match.group(2)
                        if blockNumber in notRestartedBlockNumbersAndTimes and notRestartedBlockNumbersAndTimes[blockNumber] == time:
                            print(f"Duplicate Starting block found: {blockNumber} in {f}")
                            duplicateStartFound = True
                            break
                        notRestartedBlockNumbersAndTimes[blockNumber] = time
            if duplicateStartFound:
                break

        return not duplicateStartFound

    def analyzeProduction(self, specificBlockNum=None, thresholdMs=500):
        dataDir=Utils.getNodeDataDir(self.nodeId)
        files=Node.findStderrFiles(dataDir)
        blockAnalysis={}
        anyBlockStr=r'[0-9]+'
        initialTimestamp=r'\s+([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}.[0-9]{3})\s'
        producedBlockPreStr=r'.+Produced\sblock\s+.+\s#('
        producedBlockPostStr=r')\s@\s([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}.[0-9]{3})'
        anyBlockPtrn=re.compile(initialTimestamp + producedBlockPreStr + anyBlockStr + producedBlockPostStr)
        producedBlockPtrn=re.compile(initialTimestamp + producedBlockPreStr + str(specificBlockNum) + producedBlockPostStr) if specificBlockNum is not None else anyBlockPtrn
        producedBlockDonePtrn=re.compile(initialTimestamp + r'.+Producing\sBlock\s+#' + anyBlockStr + r'\sreturned:\strue')
        for file in files:
            with open(file, 'r') as f:
                line = f.readline()
                while line:
                    readLine=True  # assume we need to read the next line before the next pass
                    match = producedBlockPtrn.search(line)
                    if match:
                        prodTimeStr = match.group(1)
                        slotTimeStr = match.group(3)
                        blockNum = int(match.group(2))

                        line = f.readline()
                        while line:
                            matchNextBlock = anyBlockPtrn.search(line)
                            if matchNextBlock:
                                readLine=False  #already have the next line ready to check on next pass
                                break

                            matchBlockActuallyProduced = producedBlockDonePtrn.search(line)
                            if matchBlockActuallyProduced:
                                prodTimeStr = matchBlockActuallyProduced.group(1)
                                break

                            line = f.readline()

                        prodTime = datetime.strptime(prodTimeStr, Utils.TimeFmt)
                        slotTime = datetime.strptime(slotTimeStr, Utils.TimeFmt)
                        delta = prodTime - slotTime
                        limit = timedelta(milliseconds=thresholdMs)
                        if delta > limit:
                            if blockNum in blockAnalysis:
                                Utils.errorExit("Found repeat production of the same block num: %d in one of the stderr files in: %s" % (blockNum, dataDir))
                            blockAnalysis[blockNum] = { "slot": slotTimeStr, "prod": prodTimeStr }

                        if specificBlockNum is not None:
                            return blockAnalysis

                    if readLine:
                        line = f.readline()

        if specificBlockNum is not None and specificBlockNum not in blockAnalysis:
            blockAnalysis[specificBlockNum] = { "slot": None, "prod": None}

        return blockAnalysis
