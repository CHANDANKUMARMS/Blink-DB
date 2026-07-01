# BlinkDB - Storage Engine (Part A)

This document describes the implementation of the storage engine for BlinkDB, a high-performance key-value database system.

## Overview

Part A implements the core storage engine of BlinkDB, which provides efficient in-memory and persistent storage for key-value pairs. The implementation supports basic operations like SET, GET, and DEL through a simple REPL interface, laying the foundation for the networked database system in Part B.

## Instructions

Build the project:

make

Start the REPL:

./repl

## System Architecture

The storage engine consists of the following components:

Memtable: An in-memory data structure that stores recent writes before flushing to disk.

SSTables: Immutable sorted files stored on disk, created from the memtable.

Compaction: Merges SSTables to optimize read and write performance.

Function Overview

main(): Initializes BlinkDB and starts the REPL for user interaction.

set(string key, string value): Stores a key-value pair in the database.

get(string key): Retrieves the value associated with the given key.

del(string key): Deletes a key-value pair from the database.

repl(): Runs the interactive command-line interface for user input.

load_data(): Loads existing data into memory at startup.

save_data(): Persists in-memory data to disk.

These functions form the core operations of BlinkDB.

