/**
 * index_iterator.cpp
 */
#include <cassert>

#include "index/index_iterator.h"

namespace cmudb {

  /*
   * NOTE: you can change the destructor/constructor method here
   * set your own input parameters
   */
  INDEX_TEMPLATE_ARGUMENTS
  INDEXITERATOR_TYPE::IndexIterator(B_PLUS_TREE_LEAF_PAGE_TYPE* leaf_page, BufferPoolManager *buffer_pool_manager, int index)
    :current_page_(leaf_page), buffer_pool_manager_(buffer_pool_manager), current_index_in_page_(index), max_size_in_current_page_(leaf_page->GetSize()) {}

  INDEX_TEMPLATE_ARGUMENTS
  INDEXITERATOR_TYPE::~IndexIterator() {
    buffer_pool_manager_->UnpinPage(current_page_->GetPageId(), false);
  }

  INDEX_TEMPLATE_ARGUMENTS
  bool INDEXITERATOR_TYPE::isEnd() {
    return current_index_in_page_ >= max_size_in_current_page_ && current_page_->GetNextPageId() == INVALID_PAGE_ID;
  }

  
  INDEX_TEMPLATE_ARGUMENTS
  const MappingType &INDEXITERATOR_TYPE::operator*() {
    return current_page_->GetItem(current_index_in_page_);
  }

  INDEX_TEMPLATE_ARGUMENTS
  INDEXITERATOR_TYPE &INDEXITERATOR_TYPE::operator++() {
    current_index_in_page_++;
    if (current_index_in_page_ >= max_size_in_current_page_) {
      int next_page_id = current_page_->GetNextPageId();
      if (next_page_id == INVALID_PAGE_ID) {
	return *this;
      }
      buffer_pool_manager_->UnpinPage(current_page_->GetPageId(), false);
      
      current_page_ = reinterpret_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(buffer_pool_manager_->FetchPage(next_page_id)->GetData());
      current_index_in_page_ = 0;
      max_size_in_current_page_ = current_page_->GetSize();
    } 

    return *this;
  }

  

  template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;
  template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;
  template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;
  template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;
  template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;
} // namespace cmudb
