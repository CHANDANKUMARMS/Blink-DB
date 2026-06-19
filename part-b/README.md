# BlinkDB - Storage Engine with TCP Network Layer (Part B)

This document describes the implementation of the storage engine for BlinkDB, a high-performance key-value database system.

## Overview

Part A implements the core storage engine of BlinkDB, which provides efficient in-memory and persistent storage for key-value pairs. The implementation supports basic operations like SET, GET, and DEL through a simple REPL interface, laying the foundation for the networked database system in Part B.

## Instructions

Build the project:

make

Start the REPL:

./server

Open new Terminal and run the reddis benchmark to port 9001:

redis-benchmark -h 127.0.0.1 -p 9001 -n 10000 -c 10 -t set,get -r 10000 > result_10000_10.txt



## System Architecture
# BlinkDB Function Descriptions

## BloomFilter Class

- **BloomFilter::BloomFilter**: Constructs a Bloom filter with a specified number of bits and hash functions.
- **BloomFilter::add**: Adds an element to the Bloom filter.
- **BloomFilter::contains**: Checks if an element might be in the Bloom filter.

## File Management Functions

- **map_file**: Memory-maps a file and returns a pointer to its contents along with the file size.
- **get_line_from_mapping**: Retrieves a full line from a memory-mapped file given an offset.
- **build_index_for_sstable**: Builds an in-memory index and Bloom filter for an SSTable file.
- **binary_search_file**: Performs a binary search on the in-memory index for a given SSTable file, utilizing a Bloom filter for quick lookups.

## BlinkDB Class

- **BlinkDB::set**: Sets a key-value pair in BlinkDB, updating existing keys or flushing the in-memory store if capacity is reached.
- **BlinkDB::get**: Retrieves the value associated with a key in BlinkDB, checking the cache, in-memory store, and SSTable files.
- **BlinkDB::del**: Deletes a key from BlinkDB, marking it as deleted in the in-memory store and as a tombstone in SSTables.
- **BlinkDB::flush**: Flushes the in-memory store to a new SSTable file, building the corresponding index and Bloom filter.
- **BlinkDB::search_sstables**: Searches SSTable files for a given key, returning the associated value or "NULL" if not found.
- **BlinkDB::compact**: Compacts multiple SSTable files into a single SSTable, merging records and removing tombstones while rebuilding indices and filters.


