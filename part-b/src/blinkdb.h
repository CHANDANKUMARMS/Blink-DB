#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "lru_cache.h" // include the LRU cache header

/**
 * @brief BlinkDB is a lightweight database with an in-memory memtable and SSTable persistence.
 *
 * It supports basic operations (set, get, delete) and uses an LRU cache to speed up read operations.
 */
class BlinkDB
{
private:
    /**
     * @brief Structure to store a value and its deletion state.
     */
    struct Entry
    {
        std::string value; ///< The stored value.
        bool deleted = false; ///< Flag indicating if the entry has been marked as deleted.
    };

    std::unordered_map<std::string, Entry> store; ///< In-memory memtable storing key-value entries.
    size_t capacity;                              ///< Maximum entries allowed in the memtable.
    size_t size;                                  ///< Current number of entries in the memtable.
    std::vector<std::string> sstables;            ///< List of SSTable file names used for persistence.

    /**
     * @brief Flushes the memtable to an SSTable file.
     *
     * This operation writes all entries in the memtable to a disk file.
     */
    void flush();

    /**
     * @brief Compacts multiple SSTable files into a single SSTable.
     *
     * This operation merges multiple SSTables, removes tombstones, and rebuilds indexes.
     */
    void compact();

    /**
     * @brief Searches for a key in the SSTable files.
     *
     * Searches from the newest to the oldest SSTable file using an optimized lookup.
     *
     * @param key The key to search for.
     * @return The value associated with the key if found; otherwise "NULL".
     */
    std::string search_sstables(const std::string &key) const;

    /**
     * @brief LRU cache instance used for fast GET operations.
     *
     * Caches key-value pairs to improve performance.
     */
    LRUCache<std::string, std::string> cache;

public:
    /**
     * @brief Constructs a BlinkDB instance.
     *
     * Initializes the memtable with a given capacity and sets up the LRU cache with its own capacity.
     *
     * @param capacity Maximum entries for the memtable (default: 64*1024*1024).
     * @param cache_capacity Capacity for the LRU cache (default: 64*1024*1024).
     */
    explicit BlinkDB(size_t capacity = 64*1024*1024, size_t cache_capacity = 64*1024*1024)
      : capacity(capacity), size(0), cache(cache_capacity) {}

    /**
     * @brief Sets a key-value pair in the database.
     *
     * If the key already exists, its value is updated.
     *
     * @param key The key to set.
     * @param value The value to associate with the key.
     */
    void set(const std::string &key, const std::string &value);

    /**
     * @brief Retrieves the value associated with a given key.
     *
     * First checks the LRU cache and then the memtable and SSTables.
     *
     * @param key The key to retrieve.
     * @return The value associated with the key, or "NULL" if the key is not found.
     */
    std::string get(const std::string &key);

    /**
     * @brief Deletes a key from the database.
     *
     * Marks the key as deleted in the memtable and removes it from the cache.
     *
     * @param key The key to delete.
     * @return true if the key was found and deleted; false otherwise.
     */
    bool del(const std::string &key);
};
