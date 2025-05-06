## Description

The `http_plugin` is a core plugin supported by both `nodeos` and `keosd`. The plugin is required to enable any RPC API functionality provided by a `nodeos` or `keosd` instance.

## Usage

```console
# config.ini
plugin = eosio::http_plugin
[options]
```
```sh
# command-line
nodeos ... --plugin eosio::http_plugin [options]
 (or)
keosd ... --plugin eosio::http_plugin [options]
```

## Options

These can be specified from both the command-line or the `config.ini` file:

```console
Config Options for eosio::http_plugin:
  --unix-socket-path arg                The filename (relative to data-dir) to
                                        create a unix socket for HTTP RPC; set
                                        blank to disable.
  --http-server-address arg (=127.0.0.1:8888)
                                        The local IP and port to listen for
                                        incoming http connections; set blank to
                                        disable.
  --http-category-address arg           The local IP and port to listen for
                                        incoming http category connections.
                                        Syntax: category,address
                                            Where the address can be
                                        <hostname>:port, <ipaddress>:port or
                                        unix socket path;
                                            in addition, unix socket path must
                                        starts with '/', './' or '../'. When
                                        relative path
                                            is used, it is relative to the data
                                        path.

                                            Valid categories include chain_ro,
                                        chain_rw, db_size, net_ro, net_rw,
                                        producer_ro
                                            producer_rw, snapshot, trace_api,
                                        prometheus, and test_control.

                                            A single `hostname:port`
                                        specification can be used by multiple
                                        categories
                                            However, two specifications having
                                        the same port with different hostname
                                        strings
                                            are always considered as
                                        configuration error regardless of
                                        whether they can be resolved
                                            into the same set of IP addresses.

                                          Examples:
                                            chain_ro,127.0.0.1:8080
                                            chain_ro,127.0.0.1:8081
                                            chain_rw,localhost:8081 # ERROR!,
                                        same port with different addresses
                                            chain_rw,[::1]:8082
                                            net_ro,localhost:8083
                                            net_rw,server.domain.net:8084
                                            producer_ro,/tmp/absolute_unix_path
                                        .sock
                                            producer_rw,./relative_unix_path.so
                                        ck
                                            trace_api,:8086 # listen on all
                                        network interfaces

                                          Notice that the behavior for `[::1]`
                                        is platform dependent. For system with
                                        IPv4 mapped IPv6 networking
                                          is enabled, using `[::1]` will listen
                                        on both IPv4 and IPv6; other systems
                                        like FreeBSD, it will only
                                          listen on IPv6. On the other hand,
                                        the specfications without hostnames
                                        like `:8086` will always listen on
                                          both IPv4 and IPv6 on all platforms.
  --access-control-allow-origin arg     Specify the Access-Control-Allow-Origin
                                        to be returned on each request
  --access-control-allow-headers arg    Specify the Access-Control-Allow-Header
                                        s to be returned on each request
  --access-control-max-age arg          Specify the Access-Control-Max-Age to
                                        be returned on each request.
  --access-control-allow-credentials    Specify if Access-Control-Allow-Credent
                                        ials: true should be returned on each
                                        request.
  --max-body-size arg (=2097152)        The maximum body size in bytes allowed
                                        for incoming RPC requests
  --http-max-bytes-in-flight-mb arg (=500)
                                        Maximum size in megabytes http_plugin
                                        should use for processing http
                                        requests. -1 for unlimited. 503 error
                                        response when exceeded.
  --http-max-in-flight-requests arg (=-1)
                                        Maximum number of requests http_plugin
                                        should use for processing http
                                        requests. 503 error response when
                                        exceeded.
  --http-max-response-time-ms arg (=15) Maximum time on main thread for
                                        processing a request, -1 for unlimited
  --verbose-http-errors                 Append the error log to HTTP responses
  --http-validate-host arg (=1)         If set to false, then any incoming
                                        "Host" header is considered valid
  --http-alias arg                      Additionally acceptable values for the
                                        "Host" header of incoming HTTP
                                        requests, can be specified multiple
                                        times.  Includes http/s_server_address
                                        by default.
  --http-threads arg (=2)               Number of worker threads in http thread
                                        pool
  --http-keep-alive arg (=1)            If set to false, do not keep HTTP
                                        connections alive, even if client
                                        requests.```

## Dependencies

None
