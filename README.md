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