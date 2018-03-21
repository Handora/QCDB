/**
 * LRU implementation
 */
#include "buffer/lru_replacer.h"
#include "page/page.h"
#include <mutex>
#include <iterator>

namespace cmudb {

  template <typename T> LRUReplacer<T>::LRUReplacer() {}

  template <typename T> LRUReplacer<T>::~LRUReplacer() {}

  /*
   * Insert value into LRU
   */
  template <typename T> void LRUReplacer<T>::Insert(const T &value) {
    std::lock_guard<std::mutex> latch(lru_replacer_latch_);
    auto itr = lru_key_itr_map_.find(value);
    if (itr == lru_key_itr_map_.end()) {
      lru_list_.push_front(value);
      lru_key_itr_map_.insert({value, lru_list_.begin()});
    } else {
      lru_list_.erase(itr->second);
      lru_list_.push_front(itr->first);
      itr->second = lru_list_.begin();
    }
  }

  /* If LRU is non-empty, pop the head member from LRU to argument "value", and
   * return true. If LRU is empty, return false
   */
  template <typename T> bool LRUReplacer<T>::Victim(T &value) {
    std::lock_guard<std::mutex> latch(lru_replacer_latch_);
    if (! lru_list_.empty()) {
      value = lru_list_.back();
      lru_list_.pop_back();
      lru_key_itr_map_.erase(value);
      return true;
    }
    return false;
  }

  /*
   * Remove value from LRU. If removal is successful, return true, otherwise
   * return false
   */
  template <typename T> bool LRUReplacer<T>::Erase(const T &value) {
    std::lock_guard<std::mutex> latch(lru_replacer_latch_); 
    auto result = lru_key_itr_map_.find(value);
    if (result != lru_key_itr_map_.end()) {
      lru_list_.erase(result->second);
      lru_key_itr_map_.erase(value); 
      return true;
    }
    return false;
  }

  template <typename T> size_t LRUReplacer<T>::Size() { return lru_list_.size(); }

  template class LRUReplacer<Page *>;
// test only
  template class LRUReplacer<int>;

} // namespace cmudb
