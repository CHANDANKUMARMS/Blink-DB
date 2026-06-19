/**
 * @file blinkdb.cpp
 * @brief Implementation of the BlinkDB key-value storage engine and REPL.
 *
 * This file implements the core functionality of the BlinkDB storage engine,
 * including the Bloom filter, in-memory indices for SSTables, and helper functions
 * for memory mapping. It also contains the main REPL for interacting with the storage engine.
 */

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
  * @class BloomFilter
  * @brief A simple Bloom filter implementation.
  *
  * This class provides methods to add an element to the filter and check if an element
  * might be present in the filter.
  */
 class BloomFilter {
 public:
     /**
      * @brief Construct a new BloomFilter object.
      * @param m Number of bits in the filter.
      * @param k Number of hash functions to use.
      */
     BloomFilter(size_t m, size_t k)
         : m_bits(m), k_hashes(k), bits(m, false) {}
 
     /**
      * @brief Adds a string element to the Bloom filter.
      * @param s The string element to add.
      */
     void add(const std::string &s) {
         for (size_t i = 0; i < k_hashes; ++i) {
             size_t hash = hash_i(s, i);
             bits[hash % m_bits] = true;
         }
     }
 
     /**
      * @brief Checks if a string element might be in the Bloom filter.
      * @param s The string element to check.
      * @return true if the element might be in the filter, false otherwise.
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
     size_t m_bits;              ///< Number of bits in the filter.
     size_t k_hashes;            ///< Number of hash functions.
     std::vector<bool> bits;     ///< Bit vector representing the Bloom filter.
 
     /**
      * @brief Hash function combining std::hash with a seed.
      * @param s The input string to hash.
      * @param i The hash function seed.
      * @return The computed hash value.
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
  * @brief Memory-map a file and return a pointer to its content along with its size.
  *
  * If the file has already been mapped, the cached mapping is returned.
  *
  * @param filename The name of the file to map.
  * @param filesize Output parameter for the size of the file.
  * @return A pointer to the mapped file data, or nullptr on error.
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
  * @brief Retrieve a full line from a memory-mapped file starting at a given offset.
  *
  * The returned line does not include the newline character.
  *
  * @param data Pointer to the mapped file data.
  * @param filesize Size of the file.
  * @param offset Offset in the file from which to start reading.
  * @return The line read from the file.
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
  * @brief Build an in-memory index for an SSTable file and create its Bloom filter.
  *
  * The function reads each record (line) from the SSTable, extracts the key, and records its file offset.
  * It then builds a Bloom filter based on the keys.
  *
  * @param filename The name of the SSTable file.
  * @return A vector of (key, file offset) pairs sorted by key.
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
  * @brief Perform a binary search on the in-memory index for a given SSTable file.
  *
  * First, the Bloom filter is checked to quickly rule out a missing key.
  * If the key might exist, a binary search is performed on the index and the corresponding record is retrieved.
  *
  * @param filename The name of the SSTable file.
  * @param key The key to search for.
  * @return The value associated with the key, or "NULL" if not found.
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
  * @brief Sets the value for a given key in the storage engine.
  *
  * This method updates the in-memory store and the LRU cache.
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
  * @brief Retrieves the value associated with a given key.
  *
  * The function first checks the LRU cache, then the in-memory store, and finally
  * the SSTable files.
  *
  * @param key The key to retrieve.
  * @return The associated value, or "NULL" if not found.
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
  * @brief Deletes the key-value pair for a given key.
  *
  * The deletion is performed in the in-memory store and, if necessary, a tombstone is added.
  *
  * @param key The key to delete.
  * @return true if the key existed and was marked for deletion, false otherwise.
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
  * @brief Flushes the in-memory store to disk as a new SSTable file.
  *
  * The function writes all key-value pairs to a file and updates the in-memory index,
  * Bloom filter, and memory mapping for the new SSTable.
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
  * @brief Searches for a key in all SSTable files.
  *
  * The search is performed from the newest to the oldest SSTable.
  *
  * @param key The key to search for.
  * @return The associated value if found, otherwise "NULL".
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
  * @brief Compacts SSTable files by merging them into a single file.
  *
  * This function merges all SSTable files into one, removes tombstones, and
  * rebuilds the in-memory indices, Bloom filters, and memory maps.
  */
 void BlinkDB::compact()
 {
     if (sstables.size() <= 3)
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
 
 // ----- Main REPL -----
 /**
  * @brief Main function providing a REPL interface for the BlinkDB storage engine.
  *
  * The REPL supports the following commands:
  * - SET <key> "<value>"
  * - GET <key>
  * - DEL <key>
  * - EXIT
  *
  * @return int Exit status code.
  */
 int main() {
    BlinkDB db;  // Create an instance of our storage engine

    std::cout << "Key-Value Storage Engine REPL" << std::endl;
    std::cout << "Commands supported:" << std::endl;
    std::cout << "  SET <key> <value>    (value may be given with or without surrounding quotes)" << std::endl;
    std::cout << "  GET <key>" << std::endl;
    std::cout << "  DEL <key>" << std::endl;
    std::cout << "  EXIT" << std::endl;

    std::string input;
    while (true) {
        std::cout << "User> ";
        std::getline(std::cin, input);
        if (input.empty())
            continue;
        std::istringstream iss(input);
        std::string command;
        iss >> command;
        if (command == "SET") {
            std::string key, firstVal;
            // Ensure that there are at least 3 arguments (command, key, and value)
            if (!(iss >> key >> firstVal)) {
                std::cout << "Invalid command." << std::endl;
                continue;
            }
            std::string value;
            // Check if the value starts with a double quote.
            if (firstVal.front() == '"') {
                // Remove the starting quote.
                value = firstVal.substr(1);
                // If the token ends with a double quote, remove it.
                if (!value.empty() && value.back() == '"') {
                    value.pop_back();
                } else {
                    std::string token;
                    bool foundEndQuote = false;
                    // Continue reading tokens until we find a token that ends with a double quote.
                    while (iss >> token) {
                        value += " " + token;
                        if (!token.empty() && token.back() == '"') {
                            // Remove the ending quote.
                            value.back() = ' ';
                            foundEndQuote = true;
                            break;
                        }
                    }
                    if (foundEndQuote) {
                        // Trim the extra space added after replacing the ending quote.
                        if (!value.empty() && value.back() == ' ')
                            value.pop_back();
                    }
                }
            } else {
                // Unquoted value: take first token and append any remaining tokens (separated by space).
                value = firstVal;
                std::string token;
                while (iss >> token) {
                    value += " " + token;
                }
            }
            // If after processing, the value is empty, treat as an invalid command.
            if (value.empty()) {
                std::cout << "Invalid command." << std::endl;
                continue;
            }
            db.set(key, value);
        } else if (command == "GET") {
            std::string key;
            iss >> key;
            std::string result = db.get(key);
            std::cout << result << std::endl;
        } else if (command == "DEL") {
            std::string key;
            iss >> key;
            bool success = db.del(key);
            if (!success)
                std::cout << "Does not exist." << std::endl;
        } else if (command == "EXIT") {
            break;
        } else {
            std::cout << "Unknown command." << std::endl;
        }
    }

    return 0;
}