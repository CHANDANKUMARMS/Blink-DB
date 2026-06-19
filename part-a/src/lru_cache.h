#ifndef LRUCACHE_H
#define LRUCACHE_H

/**
 * @file lru_cache.h
 * @brief Header file for the LRUCache template class.
 *
 * This file declares the LRUCache template class, which provides a simple
 * implementation of a least-recently-used (LRU) cache.
 */

#include <unordered_map>
#include <list>
#include <utility>

/**
 * @class LRUCache
 * @brief A template class for a Least Recently Used (LRU) cache.
 *
 * This class implements a cache that evicts the least recently used item when the capacity is reached.
 * It supports basic operations like getting, putting, and removing items.
 *
 * @tparam K Type of the key.
 * @tparam V Type of the value.
 */
template<typename K, typename V>
class LRUCache {
public:
    /**
     * @brief Constructs an LRUCache object with the specified capacity.
     * @param capacity The maximum number of items the cache can hold.
     */
    explicit LRUCache(size_t capacity) : capacity(capacity) {}

    /**
     * @brief Retrieves the value associated with a given key.
     *
     * If the key is found, the value is stored in the provided reference and the item is
     * moved to the front of the cache (marking it as most recently used).
     *
     * @param key The key to retrieve.
     * @param value Output parameter to store the retrieved value.
     * @return true if the key is found, false otherwise.
     */
    bool get(const K &key, V &value) {
        auto it = cacheMap.find(key);
        if (it == cacheMap.end())
            return false;
        // Move accessed item to the front (most recently used)
        cacheList.splice(cacheList.begin(), cacheList, it->second);
        value = it->second->second;
        return true;
    }

    /**
     * @brief Inserts or updates a key-value pair in the cache.
     *
     * If the key already exists, its value is updated and the item is moved to the front.
     * If the cache exceeds its capacity, the least recently used item is removed.
     *
     * @param key The key to insert or update.
     * @param value The value associated with the key.
     */
    void put(const K &key, const V &value) {
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            // Update value and move to front
            it->second->second = value;
            cacheList.splice(cacheList.begin(), cacheList, it->second);
            return;
        }
        // Check capacity, remove least recently used if needed
        if (cacheList.size() >= capacity) {
            auto last = cacheList.back();
            cacheMap.erase(last.first);
            cacheList.pop_back();
        }
        cacheList.emplace_front(key, value);
        cacheMap[key] = cacheList.begin();
    }

    /**
     * @brief Removes a key and its associated value from the cache.
     *
     * If the key exists, it is removed from both the internal list and map.
     *
     * @param key The key to remove.
     */
    void remove(const K &key) {
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            cacheList.erase(it->second);
            cacheMap.erase(it);
        }
    }

private:
    size_t capacity;   ///< Maximum capacity of the cache.
    std::list<std::pair<K, V>> cacheList;   ///< List storing key-value pairs; front is most recently used.
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> cacheMap; ///< Map for fast lookup of cache items.
};

#endif // LRUCACHE_H
