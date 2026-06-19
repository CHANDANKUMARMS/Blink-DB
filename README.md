# BlinkDB

BlinkDB is a compact C++ key-value database built around an LSM-tree-style storage engine. It combines an in-memory memtable with immutable SSTables, Bloom filters, memory-mapped reads, an LRU cache, tombstone-based deletion, and compaction.

The project is split into two stages:

- **Part A:** a local command-line REPL for `SET`, `GET`, and `DEL` operations.
- **Part B:** a TCP server that accepts a subset of Redis commands through RESP on port `9001`.

## Features

- In-memory memtable for recent writes
- Sorted String Tables (SSTables) for persistent storage
- Bloom filters to avoid unnecessary disk lookups
- In-memory SSTable indexes and memory-mapped file reads
- LRU caching for frequently accessed values
- Tombstone-based deletion
- Automatic SSTable compaction
- Redis-compatible `PING`, `SET`, `GET`, and `DEL` commands
- Linux `epoll`-based TCP server
- Included benchmark results and generated Doxygen documentation

## Repository layout

```text
.
├── part-a/
│   ├── src/                 # Storage engine and REPL source code
│   ├── docs/                # Design document and Doxygen output
│   ├── Makefile
│   └── README.md
└── part-b/
    ├── src/                 # Storage engine and RESP/TCP server source code
    ├── docs/                # Design document and Doxygen output
    ├── results/             # Redis benchmark results
    ├── Makefile
    └── README.md
```

## Requirements

BlinkDB uses POSIX APIs including `mmap`, `unistd`, and `epoll`. Build it on Linux or in WSL when using Windows.

- GNU Make
- GCC with C++14 support for Part A
- GCC with C++17 and POSIX threads for Part B
- Optional: `redis-cli` and `redis-benchmark` from Redis tools

On Ubuntu or WSL:

```bash
sudo apt update
sudo apt install build-essential redis-tools
```

## Part A: local storage-engine REPL

Build and start the REPL:

```bash
cd part-a
make
./repl
```

Supported commands:

```text
SET <key> <value>
GET <key>
DEL <key>
EXIT
```

Example session:

```text
User> SET language C++
User> GET language
C++
User> DEL language
User> GET language
NULL
User> EXIT
```

Remove the compiled executable with:

```bash
make clean
```

## Part B: Redis-compatible TCP server

Build and start the server:

```bash
cd part-b
make
./server
```

The server listens on `0.0.0.0:9001`. Keep it running and open another terminal to interact with it:

```bash
redis-cli -h 127.0.0.1 -p 9001 PING
redis-cli -h 127.0.0.1 -p 9001 SET greeting hello
redis-cli -h 127.0.0.1 -p 9001 GET greeting
redis-cli -h 127.0.0.1 -p 9001 DEL greeting
```

Expected responses include `PONG`, `OK`, stored values, and integer delete results.

To run a benchmark:

```bash
redis-benchmark \
  -h 127.0.0.1 \
  -p 9001 \
  -n 10000 \
  -c 10 \
  -t set,get \
  -r 10000
```

To save the benchmark output:

```bash
redis-benchmark -h 127.0.0.1 -p 9001 -n 10000 -c 10 -t set,get -r 10000 \
  > results/result_10000_10.txt
```

Stop the server with `Ctrl+C`, then remove the compiled executable with `make clean`.

## Architecture

Writes first enter the memtable and LRU cache. When the memtable reaches capacity, its records are sorted and flushed to an SSTable. Reads check the cache, then the memtable, and finally search SSTables from newest to oldest. A Bloom filter can reject missing keys before an indexed, memory-mapped lookup is attempted. Deletes are represented by tombstones until compaction merges SSTables and removes obsolete records.

Part B places a RESP parser and an `epoll`-based TCP event loop in front of the same storage engine, allowing standard Redis command-line and benchmark tools to communicate with BlinkDB.

## Notes and limitations

- The network server implements only `PING`, `SET`, `GET`, and `DEL`; it is not a complete Redis replacement.
- SSTable files are created in the current working directory at runtime.
- The server is designed for Linux because it depends on `epoll` and POSIX system calls.
- Generated documentation and the original design documents are included under each part's `docs` directory.

## Documentation

- [Part A design document](part-a/docs/24CS60R68_Part_A_DesignDoc.pdf)
- [Part A Doxygen PDF](part-a/docs/doxygen/24CS60R68_Part_A_Doxygen.pdf)
- [Part B design document](part-b/docs/24CS60R68_Part_B_DesignDoc.pdf)
- [Part B Doxygen PDF](part-b/docs/doxygen/24CS60R68_Part_B_Doxygen.pdf)

