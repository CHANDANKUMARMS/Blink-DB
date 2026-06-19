#include "blinkdb.h"
#include <fstream>
#include <algorithm>
#include <vector>
#include <map>
#include <cstdio>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <functional>
#include <cmath>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/**
 * @brief A simple Bloom filter implementation.
 *
 * This class implements a basic Bloom filter using a vector of booleans.
 */
class BloomFilter {
public:
    /**
     * @brief Constructs a BloomFilter.
     * 
     * @param m The number of bits in the filter.
     * @param k The number of hash functions.
     */
    BloomFilter(size_t m, size_t k)
        : m_bits(m), k_hashes(k), bits(m, false) {}

    /**
     * @brief Adds an element to the Bloom filter.
     * 
     * @param s The element (as a string) to add.
     */
    void add(const std::string &s) {
        for (size_t i = 0; i < k_hashes; ++i) {
            size_t hash = hash_i(s, i);
            bits[hash % m_bits] = true;
        }
    }

    /**
     * @brief Checks if an element might be in the Bloom filter.
     * 
     * @param s The element (as a string) to check.
     * @return true if the element might be in the filter, false if it definitely is not.
     */
    bool contains(const std::string &s) const {
        for (size_t i = 0; i < k_hashes; ++i) {
            size_t hash = hash_i(s, i);
            if (!bits[hash % m_bits])
                return false;
        }
        return true;
    }

private:
    size_t m_bits;           ///< The number of bits in the Bloom filter.
    size_t k_hashes;         ///< The number of hash functions.
    std::vector<bool> bits;  ///< The bit vector representing the filter.

    /**
     * @brief Generates a hash value for a given string and hash function index.
     * 
     * This combines the standard std::hash with a seed value.
     * 
     * @param s The input string.
     * @param i The index of the hash function.
     * @return The generated hash value.
     */
    size_t hash_i(const std::string &s, size_t i) const {
        return std::hash<std::string>{}(s) ^ (i * 0x9e3779b97f4a7c15ULL);
    }
};

// Global in-memory index for each SSTable file.
// For each filename, we store a sorted vector of (key, file offset) pairs.
static std::unordered_map<std::string, std::vector<std::pair<std::string, std::streampos>>> sstable_indices;

// Global Bloom filters for each SSTable file.
static std::unordered_map<std::string, BloomFilter> sstable_filters;

// Global memory-mapped regions for SSTable files.
// Each entry maps a filename to a pair: (pointer to mapped memory, file size).
static std::unordered_map<std::string, std::pair<const char*, size_t>> sstable_mmaps;

/**
 * @brief Memory-maps a file and returns a pointer to its contents along with the file size.
 *
 * If the file is already memory-mapped, it returns the existing mapping.
 *
 * @param filename The name of the file to map.
 * @param filesize Output parameter that will contain the size of the file.
 * @return Pointer to the mapped file data, or nullptr on failure.
 */
static const char* map_file(const std::string &filename, size_t &filesize) {
    // If already mapped, return it.
    if (sstable_mmaps.find(filename) != sstable_mmaps.end()) {
        filesize = sstable_mmaps[filename].second;
        return sstable_mmaps[filename].first;
    }
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1)
        return nullptr;
    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return nullptr;
    }
    filesize = st.st_size;
    const char *data = static_cast<const char*>(mmap(nullptr, filesize, PROT_READ, MAP_SHARED, fd, 0));
    close(fd);
    if (data == MAP_FAILED)
        return nullptr;
    sstable_mmaps[filename] = {data, filesize};
    return data;
}

/**
 * @brief Retrieves a full line from a memory-mapped file given an offset.
 *
 * The returned string does not include the newline character.
 *
 * @param data Pointer to the memory-mapped file.
 * @param filesize The size of the file.
 * @param offset The offset from where to start reading.
 * @return The line as a string.
 */
static std::string get_line_from_mapping(const char *data, size_t filesize, size_t offset) {
    if (offset >= filesize)
        return "";
    size_t end = offset;
    while (end < filesize && data[end] != '\n') {
        ++end;
    }
    return std::string(data + offset, end - offset);
}

/**
 * @brief Builds an in-memory index and Bloom filter for an SSTable file.
 *
 * It reads each line (record) from the file, extracts the key and file offset,
 * and then builds both the sorted index and the Bloom filter.
 *
 * @param filename The SSTable file name.
 * @return A sorted vector of (key, file offset) pairs.
 */
static std::vector<std::pair<std::string, std::streampos>> build_index_for_sstable(const std::string &filename) {
    std::vector<std::pair<std::string, std::streampos>> index;
    size_t filesize = 0;
    const char *data = map_file(filename, filesize);
    if (!data)
        return index;
    size_t offset = 0;
    while (offset < filesize) {
        std::string line = get_line_from_mapping(data, filesize, offset);
        if (!line.empty()) {
            size_t comma = line.find(',');
            if (comma != std::string::npos) {
                std::string key = line.substr(0, comma);
                index.push_back({key, static_cast<std::streampos>(offset)});
            }
        }
        // Move offset past this line (plus newline)
        offset = line.size() + 1 + offset;
    }
    // The file was written sorted; however, we sort the index to be safe.
    std::sort(index.begin(), index.end(), [](auto &a, auto &b) {
        return a.first < b.first;
    });
    // Build Bloom filter: use 10 bits per record and 3 hash functions.
    size_t m = index.size() * 10;
    size_t k = 3;
    BloomFilter bf(m, k);
    for (const auto &entry : index) {
        bf.add(entry.first);
    }
    sstable_filters.insert({filename, bf});
    return index;
}

/**
 * @brief Performs a binary search on the in-memory index for a given SSTable file.
 *
 * First checks the Bloom filter to quickly rule out missing keys, then performs a binary search
 * on the index and retrieves the record from the memory-mapped file.
 *
 * @param filename The SSTable file name.
 * @param key The key to search for.
 * @return The value corresponding to the key, or "NULL" if not found or if it's a tombstone.
 */
static std::string binary_search_file(const std::string &filename, const std::string &key) {
    // Ensure the Bloom filter and index exist.
    if (sstable_filters.find(filename) == sstable_filters.end() ||
        sstable_indices.find(filename) == sstable_indices.end()) {
        sstable_indices[filename] = build_index_for_sstable(filename);
    }
    // Instead of using operator[] (which requires a default constructor), use find().
    auto bf_it = sstable_filters.find(filename);
    if (bf_it == sstable_filters.end() || !bf_it->second.contains(key))
        return "NULL";
    
    const auto &index = sstable_indices[filename];
    int low = 0, high = static_cast<int>(index.size()) - 1;
    int mid;
    while (low <= high) {
        mid = low + (high - low) / 2;
        const std::string &file_key = index[mid].first;
        if (file_key == key) {
            size_t filesize = 0;
            const char *data = map_file(filename, filesize);
            if (!data)
                return "NULL";
            std::string line = get_line_from_mapping(data, filesize, static_cast<size_t>(index[mid].second));
            size_t comma = line.find(',');
            if (comma == std::string::npos)
                return "NULL";
            std::string file_value = line.substr(comma + 1);
            return (file_value == "TOMBSTONE") ? "NULL" : file_value;
        } else if (file_key < key) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return "NULL";
}

/**
 * @brief Sets a key-value pair in BlinkDB.
 *
 * If the key exists, its value is updated and marked as not deleted.
 * If adding a new key and capacity is reached, the in-memory store is flushed and compacted.
 *
 * @param key The key to set.
 * @param value The value to associate with the key.
 */
void BlinkDB::set(const std::string &key, const std::string &value)
{
    auto it = store.find(key);
    if (it != store.end())
    {
        it->second.value = value;
        it->second.deleted = false;
    }
    else
    {
        if (size >= capacity)
        {
            flush();
            compact();
            store.clear();
            size = 0;
        }
        store[key] = {value, false};
        size++;
    }
    // Update the LRU cache with the new value.
    cache.put(key, value);
}

/**
 * @brief Retrieves the value associated with a key in BlinkDB.
 *
 * The method first checks the LRU cache, then the in-memory store,
 * and finally searches the SSTable files using an optimized lookup.
 *
 * @param key The key to retrieve.
 * @return The associated value, or "NULL" if the key is not found or marked as deleted.
 */
std::string BlinkDB::get(const std::string &key)
{
    std::string value;
    // Check the LRU cache first.
    if (cache.get(key, value))
    {
        return value;
    }

    auto it = store.find(key);
    if (it != store.end())
    {
        if (!it->second.deleted)
        {
            cache.put(key, it->second.value);
            return it->second.value;
        }
        else
        {
            return "NULL";
        }
    }

    // Use the in-memory index, Bloom filter, and memory-mapped file to quickly locate the key in SSTables.
    for (auto it = sstables.rbegin(); it != sstables.rend(); ++it)
    {
        value = binary_search_file(*it, key);
        if (value != "NULL")
        {
            cache.put(key, value);
            return value;
        }
    }
    return "NULL";
}

/**
 * @brief Deletes a key from BlinkDB.
 *
 * The key is removed from the cache and marked as deleted in the in-memory store.
 * If the key exists in any SSTable, it is also marked as a tombstone.
 *
 * @param key The key to delete.
 * @return true if the key was found and deleted, false otherwise.
 */
bool BlinkDB::del(const std::string &key)
{
    // Remove key from the cache.
    cache.remove(key);

    auto it = store.find(key);
    if (it != store.end())
    {
        it->second.deleted = true;
        return true;
    }

    std::string value = "NULL";
    // Use the optimized lookup for deletion.
    for (auto it = sstables.rbegin(); it != sstables.rend(); ++it)
    {
        value = binary_search_file(*it, key);
        if (value != "NULL")
            break;
    }
    if (value == "NULL")
        return false;

    if (size >= capacity)
    {
        flush();
        compact();
        store.clear();
        size = 0;
    }
    store[key] = {"", true};
    size++;
    return true;
}

/**
 * @brief Flushes the in-memory store to a new SSTable file.
 *
 * The in-memory store is written to disk, and a new SSTable is created.
 * The in-memory index, Bloom filter, and memory map are built for the new file.
 */
void BlinkDB::flush()
{
    if (store.empty())
        return;

    static int sst_counter = 0;
    std::string filename = "sst_" + std::to_string(++sst_counter) + ".sst";
    std::vector<std::string> keys;
    for (const auto &pair : store)
        keys.push_back(pair.first);
    std::sort(keys.begin(), keys.end());

    std::ofstream file(filename);
    for (const auto &key : keys)
    {
        const Entry &entry = store[key];
        file << key << "," << (entry.deleted ? "TOMBSTONE" : entry.value) << "\n";
    }
    file.close();
    sstables.push_back(filename);
    // Build the in-memory index, Bloom filter, and memory map for the new SSTable.
    sstable_indices[filename] = build_index_for_sstable(filename);
    size_t filesize = 0;
    map_file(filename, filesize);
}

/**
 * @brief Searches SSTable files for a given key.
 *
 * The method searches from the newest to the oldest SSTable using the optimized lookup.
 *
 * @param key The key to search for.
 * @return The associated value, or "NULL" if not found.
 */
std::string BlinkDB::search_sstables(const std::string &key) const
{
    std::string value = "NULL";
    // Search from newest to oldest SSTable using the optimized lookup.
    for (auto it = sstables.rbegin(); it != sstables.rend(); ++it)
    {
        value = binary_search_file(*it, key);
        if (value != "NULL")
            return value;
    }
    return value;
}

/**
 * @brief Compacts multiple SSTable files into a single SSTable.
 *
 * If the number of SSTable files exceeds a threshold (10), all files are merged.
 * The merge process combines records, overwrites older entries with newer ones,
 * removes tombstones, and rebuilds the in-memory indices, Bloom filters, and memory maps.
 */
void BlinkDB::compact()
{
    if (sstables.size() <= 10)
        return;

    static int compact_counter = 0;
    std::string new_filename = "sst_compact_" + std::to_string(++compact_counter) + ".sst";

    std::map<std::string, std::string> merged;
    for (const auto &filename : sstables)
    {
        std::ifstream file(filename);
        std::string line;
        while (std::getline(file, line))
        {
            size_t comma = line.find(',');
            if (comma == std::string::npos)
                continue;
            std::string key = line.substr(0, comma);
            std::string value = line.substr(comma + 1);
            merged[key] = value; // Overwrite with newer entries
        }
    }

    // Remove tombstones.
    for (auto it = merged.begin(); it != merged.end();)
    {
        if (it->second == "TOMBSTONE")
            it = merged.erase(it);
        else
            ++it;
    }

    std::ofstream new_file(new_filename);
    for (const auto &pair : merged)
    {
        new_file << pair.first << "," << pair.second << "\n";
    }
    new_file.close();

    // Remove old SSTable files.
    for (const auto &filename : sstables)
    {
        std::remove(filename.c_str());
    }
    sstables = {new_filename};

    // Clear and rebuild the in-memory indices, Bloom filters, and memory maps.
    sstable_indices.clear();
    sstable_filters.clear();
    // Unmap old files.
    for (auto &p : sstable_mmaps) {
        munmap(const_cast<char*>(p.second.first), p.second.second);
    }
    sstable_mmaps.clear();

    sstable_indices[new_filename] = build_index_for_sstable(new_filename);
}
