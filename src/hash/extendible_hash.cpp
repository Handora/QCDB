#include <list>

#include "hash/extendible_hash.h"
#include "page/page.h"
#include "common/logger.h"
#include <functional>
#include <cassert>

namespace cmudb {

/*
 * constructor
 * array_size: fixed array size for each bucket
 */
template <typename K, typename V>
ExtendibleHash<K, V>::ExtendibleHash(size_t size): size_(size) {
  bucket_address_table_.push_back(std::make_shared<Bucket>(0));
}

/*
 * helper function to calculate the hashing address of input key
 */
template <typename K, typename V>
size_t ExtendibleHash<K, V>::HashKey(const K &key) {
  return std::hash<K>{}(key) % (1 << global_depth_);
}

/*
 * helper function to return global depth of hash table
 * NOTE: you must implement this function in order to pass test
 */
template <typename K, typename V>
int ExtendibleHash<K, V>::GetGlobalDepth() const {
  // fro RAII style mutex
  std::lock_guard<std::mutex> latch(global_depth_latch_);
  return global_depth_;
}
  
/*
 * helper function to return local depth of one specific bucket
 * NOTE: you must implement this function in order to pass test
 */
template <typename K, typename V>
int ExtendibleHash<K, V>::GetLocalDepth(int bucket_id) const {
  assert(bucket_id < (1 << global_depth_));
  assert(bucket_id >= 0);
  std::shared_ptr<Bucket> bucket = bucket_address_table_[bucket_id];
  assert(bucket != nullptr);

  std::lock_guard<std::mutex> latch(bucket->local_latch_);
  return bucket->local_depth_;
}
  
/*
 * helper function to return current number of bucket in hash table
 */
template <typename K, typename V>
int ExtendibleHash<K, V>::GetNumBuckets() const {
  std::lock_guard<std::mutex> latch(global_num_buckets_latch_);
  return 0;
}

/*
 * lookup function to find value associate with input key
 */
template <typename K, typename V>
bool ExtendibleHash<K, V>::Find(const K &key, V &value) {
  size_t bucket_id = HashKey(key);
  assert(bucket_id < (1 << global_depth_));
  assert(bucket_id >= 0);
  std::shared_ptr<Bucket> bucket = bucket_address_table_[bucket_id];
  assert(bucket != nullptr);  

  return false;
}
  
/*
 * delete <key,value> entry in hash table
 * Shrink & Combination is not required for this project
 */
template <typename K, typename V>
bool ExtendibleHash<K, V>::Remove(const K &key) {
  return false;
}

/*
 * insert <key,value> entry in hash table
 * Split & Redistribute bucket when there is overflow and if necessary increase
 * global depth
 */
template <typename K, typename V>
void ExtendibleHash<K, V>::Insert(const K &key, const V &value) {}

template class ExtendibleHash<page_id_t, Page *>;
template class ExtendibleHash<Page *, std::list<Page *>::iterator>;
// test purpose
template class ExtendibleHash<int, std::string>;
template class ExtendibleHash<int, std::list<int>::iterator>;
template class ExtendibleHash<int, int>;
} // namespace cmudb
