/*
 * extendible_hash.h : implementation of in-memory hash table using extendible
 * hashing
 *
 * Functionality: The buffer pool manager must maintain a page table to be able
 * to quickly map a PageId to its corresponding memory location; or alternately
 * report that the PageId does not match any currently-buffered page.
 */

#pragma once

#include <cstdlib>
#include <vector>
#include <string>
#include <utility>
#include <memory>
#include <mutex>

#include "hash/hash_table.h"

namespace cmudb {

  template <typename K, typename V>
    class ExtendibleHash : public HashTable<K, V> {
  public:
    // constructor
    ExtendibleHash(size_t size);
    // helper function to generate hash addressing
    size_t HashKey(const K &key);
    // helper function to get global & local depth
    int GetGlobalDepth() const;
    int GetLocalDepth(int bucket_id) const;
    int GetNumBuckets() const;
    // lookup and modifier
    bool Find(const K &key, V &value) override;
    bool Remove(const K &key) override;
    void Insert(const K &key, const V &value) override;
    void Speak(int bucket_id) const;
    void SpeakAll() const;
    
  private:
    struct Bucket {
      int local_depth_; 
      std::vector<std::pair<K, V>>  kv_records_;
      Bucket(int local_depth) {
	this->local_depth_ = local_depth;
	this->kv_records_ = {};
      }
    };
    size_t size_;
    int global_depth_ = 0;

	  // buckets number, must be 2^n
	  int num_buckets_ = 1;
    std::vector<std::shared_ptr<Bucket>> bucket_address_table_;

    // latches
    // TODO:
    //    use finer granularity(such as per buckets per locks)
    /* mutable std::mutex global_depth_latch_; */
    /* mutable std::mutex global_num_buckets_latch_; */
    /* mutable std::vector<std::mutex> bucket_latch_table_; */
    /* std::condition_variable condition_; */

    mutable std::mutex global_table_lock_;
  };
} // namespace cmudb
