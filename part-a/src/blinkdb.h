/**
 * @file blinkdb.h
 * @brief Header file for the BlinkDB key-value storage engine.
 *
 * This file declares the BlinkDB class which implements a simple key-value storage
 * engine with in-memory memtable and SSTable persistence, along with an LRU cache for fast GET operations.
 */

 #pragma once

 #include <string>
 #include <unordered_map>
 #include <vector>
 #include "lru_cache.h" // include the LRU cache header
 
 /**
  * @class BlinkDB
  * @brief A simple key-value storage engine.
  *
  * The BlinkDB class provides functionality for setting, getting, and deleting key-value pairs.
  * It uses an in-memory memtable and persistent SSTable files. It also leverages an LRU cache
  * for fast retrieval of frequently accessed keys.
  */
 class BlinkDB
 {
 private:
     /**
      * @struct Entry
      * @brief Represents an entry in the in-memory memtable.
      *
      * Each entry holds a string value and a flag indicating whether it has been deleted.
      */
     struct Entry
     {
         std::string value; ///< The value associated with the key.
         bool deleted = false; ///< Flag indicating if the entry is marked as deleted.
     };
 
     std::unordered_map<std::string, Entry> store; ///< In-memory memtable.
     size_t capacity;                              ///< Maximum number of entries in the memtable.
     size_t size;                                  ///< Current number of entries in the memtable.
     std::vector<std::string> sstables;            ///< List of SSTable file names.
 
     /**
      * @brief Flushes the in-memory memtable to an SSTable file.
      *
      * This function writes the current memtable to disk as an SSTable file.
      */
     void flush();
 
     /**
      * @brief Compacts multiple SSTable files into a single SSTable.
      *
      * This function merges SSTable files and removes obsolete entries.
      */
     void compact();
 
     /**
      * @brief Searches for a key in all SSTable files.
      *
      * The search is performed on the SSTable files if the key is not found in the memtable.
      *
      * @param key The key to search for.
      * @return The value associated with the key, or "NULL" if not found.
      */
     std::string search_sstables(const std::string &key) const;
 
     /**
      * @brief LRU cache used to speed up GET operations.
      */
     LRUCache<std::string, std::string> cache;
 
 public:
     /**
      * @brief Constructs a BlinkDB instance.
      *
      * The constructor initializes the memtable capacity and the LRU cache capacity.
      *
      * @param capacity Maximum number of entries for the memtable. Default is 3.
      * @param cache_capacity Capacity for the LRU cache. Default is 10.
      */
     explicit BlinkDB(size_t capacity = 100, size_t cache_capacity = 100)
       : capacity(capacity), size(0), cache(cache_capacity) {}
 
     /**
      * @brief Sets the value for a given key.
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
      * The function checks the memtable, cache, and SSTable files in order.
      *
      * @param key The key to retrieve.
      * @return The value associated with the key, or "NULL" if not found.
      */
     std::string get(const std::string &key);
 
     /**
      * @brief Deletes a key-value pair.
      *
      * The key is marked as deleted in the memtable.
      *
      * @param key The key to delete.
      * @return true if the key existed and was marked for deletion, false otherwise.
      */
     bool del(const std::string &key);
 };
 