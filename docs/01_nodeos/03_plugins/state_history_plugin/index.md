
## Description

The `state_history_plugin` is useful for capturing historical data about the blockchain state. The plugin receives blockchain data from other connected nodes and caches the data into files. The plugin listens on a socket for applications to connect and sends blockchain data back based on the plugin options specified when starting `nodeos`.

## Usage

```console
# config.ini
plugin = eosio::state_history_plugin
[options]
```
```sh
# command-line
nodeos ... --plugin eosio::state_history_plugin [operations] [options]
```

## Operations

These can only be specified from the `nodeos` command-line:

```console
Command Line Options for eosio::state_history_plugin:

  --delete-state-history                clear state history files
```

## Options

These can be specified from both the `nodeos` command-line or the `config.ini` file:

```console
Config Options for eosio::state_history_plugin:
  --state-history-dir arg (="state-history")
                                        the location of the state-history
                                        directory (absolute path or relative to
                                        application data dir)
  --state-history-retained-dir arg      the location of the state history
                                        retained directory (absolute path or
                                        relative to state-history dir).
  --state-history-archive-dir arg       the location of the state history
                                        archive directory (absolute path or
                                        relative to state-history dir).
                                        If the value is empty string, blocks
                                        files beyond the retained limit will be
                                        deleted.
                                        All files in the archive directory are
                                        completely under user's control, i.e.
                                        they won't be accessed by nodeos
                                        anymore.
  --state-history-stride arg            split the state history log files when
                                        the block number is the multiple of the
                                        stride
                                        When the stride is reached, the current
                                        history log and index will be renamed
                                        '*-history-<start num>-<end
                                        num>.log/index'
                                        and a new current history log and index
                                        will be created with the most recent
                                        blocks. All files following
                                        this format will be used to construct
                                        an extended history log.
  --max-retained-history-files arg      the maximum number of history file
                                        groups to retain so that the blocks in
                                        those files can be queried.
                                        When the number is reached, the oldest
                                        history file would be moved to archive
                                        dir or deleted if the archive dir is
                                        empty.
                                        The retained history log files should
                                        not be manipulated by users.
  --trace-history                       enable trace history
  --chain-state-history                 enable chain state history
  --finality-data-history               enable finality data history
  --state-history-endpoint arg (=127.0.0.1:8080)
                                        the endpoint upon which to listen for
                                        incoming connections. Caution: only
                                        expose this port to your internal
                                        network.
  --state-history-unix-socket-path arg  the path (relative to data-dir) to
                                        create a unix socket upon which to
                                        listen for incoming connections.
  --trace-history-debug-mode            enable debug mode for trace history
  --state-history-log-retain-blocks arg if set, periodically prune the state
                                        history files to store only configured
                                        number of most recent blocks
```

## How-To Guides

* [How to fast start without history on existing chains](10_how-to-fast-start-without-old-history.md)
* [How to replay or resync with full history](20_how-to-replay-or-resync-with-full-history.md)
* [How to create a portable snapshot with full state history](30_how-to-create-snapshot-with-full-history.md)
* [How to restore a portable snapshot with full state history](40_how-to-restore-snapshot-with-full-history.md)
