#ifndef LRUCACHE_H
#define LRUCACHE_H

#include <unordered_map>
#include <list>
#include <utility>

/**
 * @brief A templated Least Recently Used (LRU) Cache.
 *
 * This class implements an LRU cache using a combination of an unordered_map
 * and a doubly-linked list. The cache evicts the least recently used item
 * when its capacity is reached.
 *
 * @tparam K Type for the keys.
 * @tparam V Type for the values.
 */
template<typename K, typename V>
class LRUCache {
public:
    /**
     * @brief Constructs an LRUCache with a specified capacity.
     *
     * @param capacity The maximum number of elements the cache can hold.
     */
    explicit LRUCache(size_t capacity) : capacity(capacity) {}

    /**
     * @brief Retrieves a value by key from the cache.
     *
     * If the key is found, the corresponding item is moved to the front
     * (most recently used) and the value is returned via the output parameter.
     *
     * @param key The key to retrieve.
     * @param value Output parameter that will contain the retrieved value.
     * @return true if the key was found, false otherwise.
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
     * If the key exists, its value is updated and the item is moved to the front.
     * If the key does not exist and the cache is full, the least recently used
     * item is removed before the new key-value pair is inserted.
     *
     * @param key The key to insert or update.
     * @param value The value to associate with the key.
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
     * @brief Removes a key from the cache.
     *
     * If the key exists in the cache, it is removed.
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
    size_t capacity; ///< Maximum capacity of the cache.
    std::list<std::pair<K, V>> cacheList; ///< List to maintain order of usage.
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> cacheMap; ///< Map to store keys and corresponding list iterators.
};

#endif // LRUCACHE_H
