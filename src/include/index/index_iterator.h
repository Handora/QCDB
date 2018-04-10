/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include "page/b_plus_tree_leaf_page.h"

namespace cmudb {

#define INDEXITERATOR_TYPE                                                     \
  IndexIterator<KeyType, ValueType, KeyComparator>

INDEX_TEMPLATE_ARGUMENTS
class IndexIterator {
public:
    // you may define your own constructor based on your member variables
  IndexIterator(B_PLUS_TREE_LEAF_PAGE_TYPE* leaf_page, BufferPoolManager* buffer_pool_manager, int index=0);
  ~IndexIterator();

  bool isEnd();

  const MappingType &operator*();

  IndexIterator &operator++();

private:
  // add your own private member variables here
  B_PLUS_TREE_LEAF_PAGE_TYPE *current_page_;
  BufferPoolManager *buffer_pool_manager_;
  int current_index_in_page_;
  int max_size_in_current_page_;
};

} // namespace cmudb
