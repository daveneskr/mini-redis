# Mini Redis

Mini Redis is a small Redis-inspired in-memory key-value server written in C.

## Goals

- Learn how Redis works internally
- Build a TCP server in C
- Parse Redis-like commands
- Store key-value pairs in memory
- Implement basic commands such as PING, SET, GET, DEL, EXISTS, EXPIRE, and TTL
- Practice unit testing, Docker, Git, and project planning

## Non-goals

This project is not intended to be production-ready Redis. It will not initially support clustering, replication, persistence, Lua scripting, pub/sub, or full RESP compatibility.

## Version 0.1 Features

- Single-client TCP server
- In-memory hash table
- Basic command parser
- PING
- SET
- GET
- DEL
- Unit tests

## Planned Features

- RESP parser
- Key expiration
- Multiple clients
- Persistence
- Benchmarking
- Dockerized deployment
- 
## Supported commands

Only commands supported by the existing string key/value store are implemented:

- `PING [message]`
- `ECHO message`
- `SET key value`
- `GET key`
- `DEL key`
- `EXISTS key`
- 
## Server behavior

- IPv4 TCP listener, loopback by default
- configurable bind address and port
- one active blocking client at a time
- sequential client reconnects with one persistent Store
- fragmented TCP request buffering
- multiple buffered/pipelined command processing
- complete response writes with EINTR and partial-send handling
- SIGPIPE protection on Linux/macOS
- SIGINT/SIGTERM shutdown
- malformed/oversized request error response followed by connection close
- CLI options: `--bind`, `--port`, `--once`, `--help`

## Event loop

- Fixed RESP parsing when a request is split between `\r` and `\n`.
- Added command-specific reply-capacity preflight so mutating commands never
  execute when their reply cannot fit, while preserving support for smaller
  caller-provided buffers when they are sufficient.
- Added a shared `mini_redis_limits.h` boundary for per-client reply sizing.
- Completed the internal `Server` lifecycle:
    - `server_initialize`
    - `server_event_loop`
    - `server_destroy`
- Added `server_set_nonblocking` using `fcntl` while preserving existing flags.
- Replaced the sequential accept loop in `server_run` with a `poll()` loop.
- Added bounded per-client input and output state.
- Added one-reply-at-a-time backpressure for safe ordered pipelining.
- Added partial non-blocking sends using `output_offset`.
- Added accept/read/write fairness budgets per event-loop iteration.
- Added maximum-client enforcement.
- Defined `--once` as one accepted client followed by a drained clean exit.
- Added signal-driven shutdown with a finite poll timeout.
- Added connection, command, protocol, rejection, receive, and send metrics.
- Kept `server_handle_client` and `server_send_all` as legacy blocking helpers
  for compatibility with existing focused tests.
- Added `MINI_REDIS_BUILD_TESTS=OFF` so production builds do not need to fetch
  Unity.

## Closing semantics

- `peer_eof`: the peer cannot send more bytes.
- `close_requested`: close after any pending reply has drained.
- Protocol errors queue an RESP error, discard remaining input, and close after
  the error is sent.
- Fatal socket errors remove the client immediately.

## Backpressure invariant

A client stores at most one reply. While output is pending, the server does not
parse or execute another command for that client. Once the reply is fully sent,
already-buffered pipelined input resumes in order.

## Added tests

- `test_resp_fragmentation.c`
    - checks every proper prefix of valid RESP requests;
    - specifically covers splits between `\r` and `\n`.
- `test_request_processor_atomicity.c`
    - verifies insufficient output capacity does not execute `DEL`;
    - verifies a smaller, sufficient PING reply buffer remains supported.
- `test_server_nonblocking.c`
    - checks `O_NONBLOCK` and preservation of existing descriptor flags.
- `test_server_event_loop.c`
    - checks idle-client isolation;
    - fragmented requests;
    - shared store state;
    - ordered pipelining.

## Build

Production-only build without downloading Unity:

```bash
cmake -S . -B build -DMINI_REDIS_BUILD_TESTS=OFF
cmake --build build
```

Normal project build with tests:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
