#include <list>

#include "hash/extendible_hash.h"
#include "page/page.h"
#include "common/logger.h"
#include <functional>
#include <cassert>
#include <algorithm>
#include <utility>
#include <iterator>

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
  std::lock_guard<std::mutex> latch(global_table_lock_);
  return std::hash<K>{}(key) % (1 << global_depth_);
}

/*
 * helper function to return global depth of hash table
 * NOTE: you must implement this function in order to pass test
 */
template <typename K, typename V>
int ExtendibleHash<K, V>::GetGlobalDepth() const {
  // for RAII style mutex
  std::lock_guard<std::mutex> latch(global_table_lock_);
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
  
  std::lock_guard<std::mutex> latch(global_table_lock_);
  std::shared_ptr<Bucket> bucket = bucket_address_table_[bucket_id];
  assert(bucket != nullptr);
  
  return bucket->local_depth_;
}
  
/*
 * helper function to return current number of bucket in hash table
 */
template <typename K, typename V>
int ExtendibleHash<K, V>::GetNumBuckets() const {
  std::lock_guard<std::mutex> latch(global_table_lock_);
  return num_buckets_;
}

/*
 * lookup function to find value associate with input key
 */
template <typename K, typename V>
bool ExtendibleHash<K, V>::Find(const K &key, V &value) {
  std::lock_guard<std::mutex> latch(global_table_lock_);
  size_t bucket_id = std::hash<K>{}(key) % (1 << global_depth_);
  
  std::shared_ptr<Bucket> bucket = bucket_address_table_[bucket_id];
  assert(bucket != nullptr);
  
  auto it = std::find_if(bucket->kv_records_.begin(),
			 bucket->kv_records_.end(),
			 [&](const std::pair<K, V>& p) {
			   return p.first == key;
			 });

  if (it != bucket->kv_records_.end()) {
    value = it->second;
    return true;
  } else {
    return false;
  }
}
  
/*
 * delete <key,value> entry in hash table
 * Shrink & Combination is not required for this project
 */
template <typename K, typename V>
bool ExtendibleHash<K, V>::Remove(const K &key) {
  std::lock_guard<std::mutex> latch(global_table_lock_);

  size_t bucket_id = std::hash<K>{}(key) % (1 << global_depth_);
  
  auto bucket = bucket_address_table_[bucket_id];
  assert(bucket != nullptr);

  auto origin_length = bucket->kv_records_.size();
  remove_if(bucket->kv_records_.begin(),
	    bucket->kv_records_.end(),
	    [&](const std::pair<K, V>& record) {
	      return record.first == key;
	    });
  return origin_length != bucket->kv_records_.size();
}

/*
 * inserpppt <key,value> entry in hash table
 * Split & Redistribute bucket when there is overflow and if necessary increase
 * global depth
 */
template <typename K, typename V>
void ExtendibleHash<K, V>::Insert(const K &key, const V &value) {
  std::lock_guard<std::mutex> latch(global_table_lock_);
  std::hash<K> hasher{};
  
  size_t bucket_id = hasher(key) % (1 << global_depth_);
  
  auto bucket = bucket_address_table_[bucket_id];
  assert(bucket != nullptr);

  auto it = std::find_if(bucket->kv_records_.begin(),
			 bucket->kv_records_.end(),
			 [&](const std::pair<K, V>& p) {
			   return p.first == key;
			 });

  if (it != bucket->kv_records_.end()) {
    // overwrite when already exits
    it->second = value;
    return ;
  }
  
  if (bucket->kv_records_.size() < size_) {
    // push into bucket when not full
    bucket->kv_records_.push_back(std::make_pair(key, value));
    return ;
  }

  if (global_depth_ > bucket->local_depth_) {
    // split the bucket pointer 
    bucket->local_depth_ += 1;
    auto new_bucket = std::make_shared<Bucket>(bucket->local_depth_);
    auto records = std::move(bucket->kv_records_); 
    assert(bucket->kv_records_.size() == 0);
    records.push_back(std::make_pair(key, value));
    for (const auto & record: records) {
      if (hasher(record.first) & (1 << bucket->local_depth_)) {
	new_bucket->kv_records_.push_back(std::move(record));
      } else {
	bucket->kv_records_.push_back(std::move(record));
      }
    }

    int local_id = bucket_id % (1 << (bucket->local_depth_-1));
    // TODO:
    //   can we do better?
    for (int i=0; i<(1<<global_depth_); i++) {
      if ((i & local_id) == local_id) {
	if (i & bucket->local_depth_) {
	  bucket_address_table_[i] = new_bucket;
	} else {
	  bucket_address_table_[i] = bucket;
	}
      }
    }
    return ;
  }

  // expand the global table
  num_buckets_ *= 2;
  bucket_address_table_.resize(num_buckets_);
  global_depth_++;
  for (int i = num_buckets_/2; i < num_buckets_; ++i) {
    bucket_address_table_[i] = bucket_address_table_[i-num_buckets_/2];
  }
  bucket->local_depth_ += 1;
  auto new_bucket = std::make_shared<Bucket>(bucket->local_depth_);
  auto records = std::move(bucket->kv_records_); 
  assert(bucket->kv_records_.size() == 0);
  records.push_back(std::make_pair(key, value));
  for (const auto & record: records) {
    if (hasher(record.first) & (1 << bucket->local_depth_)) {
      new_bucket->kv_records_.push_back(std::move(record));
    } else {
      bucket->kv_records_.push_back(std::move(record));
    }
  }

  int local_id = bucket_id % (1 << (bucket->local_depth_-1));
  // TODO:
  //   can we do better?
  for (int i=0; i<(1<<global_depth_); i++) {
    if ((i & local_id) == local_id) {
      if (i & bucket->local_depth_) {
	bucket_address_table_[i] = new_bucket;
      } else {
	bucket_address_table_[i] = bucket;
      }
    }
  }
  return ;
}

// ===================== Helper Methods =================================
  // debug only
  template <typename K, typename V>
  void ExtendibleHash<K, V>::Speak(int bucket_id) const {
    std::lock_guard<std::mutex> latch(global_table_lock_);
    assert(bucket_id < (1 << global_depth_));
    assert(bucket_id >= 0);

    std::shared_ptr<Bucket> bucket = bucket_address_table_[bucket_id];
    assert(bucket != nullptr);
    for (const auto & record: bucket->kv_records_) {
      std::cout << "(" << record.first << ", " << "), " << std::endl;
    }
    std::cout << std::endl;
  }
  

  template class ExtendibleHash<page_id_t, Page *>;
  template class ExtendibleHash<Page *, std::list<Page *>::iterator>;
// test purpose
  template class ExtendibleHash<int, std::string>;
  template class ExtendibleHash<int, std::list<int>::iterator>;
  template class ExtendibleHash<int, int>;
} // namespace cmudb
