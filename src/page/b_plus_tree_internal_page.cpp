/**
 * b_plus_tree_internal_page.cpp
 */
#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "page/b_plus_tree_internal_page.h"

namespace cmudb {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/
/*
 * Init method after creating a new internal page
 * Including set page type, set current size, set page id, set parent id and set
 * max page size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(page_id_t page_id,
					  page_id_t parent_id) {
  SetPageId(page_id);
  SetParentPageId(parent_id);
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetSize(0);
  SetMaxSize((PAGE_SIZE - 20) / (sizeof(KeyType)+sizeof(ValueType)));
}
/*
 * Helper method to get/set the key associated with input "index"(a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
KeyType B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const {
  // because the key array in the internalnode is not full
  // but we can use it for other purpose
  assert(index >= 0 && index < GetMaxSize());
  
  KeyType key = array[index].first;
  return key;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  // because the key array in the internalnode is not full
  assert(index > 0 && index < GetMaxSize());
  
  array[index].first = key;
}

/*
 * Helper method to find and return array index(or offset), so that its value
 * equals to input "value"
 */
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(const ValueType &value) const {
  for (int i = 0; i < GetSize(); ++i) {
    if (array[i].second == value)
      return i;
  }

  return -1;
}

/*
 * Helper method to get the value associated with input "index"(a.k.a array
 * offset)
 */
INDEX_TEMPLATE_ARGUMENTS
ValueType B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const {
  // value index can start with 0
  assert(index >= 0 && index < GetMaxSize());
  return array[index].second;
}

/*****************************************************************************
 * LOOKUP
 *****************************************************************************/
/*
 * Find and return the child pointer(page_id) which points to the child page
 * that contains input "key"
 * Start the search from the second key(the first key should always be invalid)
 */
INDEX_TEMPLATE_ARGUMENTS
ValueType
B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(const KeyType &key,
                                       const KeyComparator &comparator) const {
  
  for (int i = 1; i < GetSize(); ++i) {
    if (comparator(key, array[i].first) < 0) {
      return array[i-1].second;
    }
  }
  
  return array[GetSize()-1].second;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Populate new root page with old_value + new_key & new_value
 * When the insertion cause overflow from leaf page all the way upto the root
 * page, you should create a new root page and populate its elements.
 * NOTE: This method is only called within InsertIntoParent()(b_plus_tree.cpp)
 */
  INDEX_TEMPLATE_ARGUMENTS
  void B_PLUS_TREE_INTERNAL_PAGE_TYPE::PopulateNewRoot(
    const ValueType &old_value, const KeyType &new_key,
    const ValueType &new_value) {
    SetSize(2);
    array[0] = std::make_pair(new_key, old_value);
    array[1] = std::make_pair(new_key, new_value); 
  }
/*
 * Insert new_key & new_value pair right after the pair with its value ==
 * old_value
 * @return:  new size after insertion
 */
  INDEX_TEMPLATE_ARGUMENTS
  int B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertNodeAfter(
    const ValueType &old_value, const KeyType &new_key,
    const ValueType &new_value) {
    assert(GetSize() < GetMaxSize()); 
    
    int old_index = ValueIndex(old_value);
    assert(old_index != -1);
    // use memmove for overlapping area
    memmove(array+old_index+2, array+old_index+1, (GetSize()-old_index-1)*(sizeof(MappingType)));
    array[old_index+1].first = new_key;
    array[old_index+1].second = new_value;
    IncreaseSize(1); 
    return GetSize();
  }

/*****************************************************************************
 * SPLIT
 *****************************************************************************/
/*
 * Remove half of key & value pairs from this page to "recipient" page
 */
  INDEX_TEMPLATE_ARGUMENTS
  void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveHalfTo(
    BPlusTreeInternalPage *recipient,
    BufferPoolManager *buffer_pool_manager) {
    int origin_size = GetSize();
    SetSize(GetSize()/2); 
    recipient->CopyHalfFrom(array+GetSize(), origin_size-GetSize(), buffer_pool_manager); 
  }

  INDEX_TEMPLATE_ARGUMENTS
  void B_PLUS_TREE_INTERNAL_PAGE_TYPE::CopyHalfFrom(
    MappingType *items, int size, BufferPoolManager *buffer_pool_manager) {
    for (int i=0; i < size; ++i) {
      // the recipient is a brand new page 
      auto child_page = reinterpret_cast<BPlusTreePage *>(buffer_pool_manager->FetchPage(items[i].second));
      child_page->SetParentPageId(GetPageId());
      buffer_pool_manager->UnpinPage(child_page->GetPageId(), true);
    }

    memmove(array, items, size*sizeof(MappingType));
    IncreaseSize(size); 
  }

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Remove the key & value pair in internal page according to input index(a.k.a
 * array offset)
 * NOTE: store key&value pair continuously after deletion
 */
  INDEX_TEMPLATE_ARGUMENTS
  void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Remove(int index) {
    std::memmove(array+index, array+index+1, (GetSize()-index-1)*(sizeof(MappingType)));
    IncreaseSize(-1);
  }

/*
 * Remove the only key & value pair in internal page and return the value
 * NOTE: only call this method within AdjustRoot()(in b_plus_tree.cpp)
 */
  INDEX_TEMPLATE_ARGUMENTS
  ValueType B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveAndReturnOnlyChild() {
    assert(GetSize() == 1);
    IncreaseSize(-1); 
    return array[0].second;
  }
/*****************************************************************************
 * MERGE
 *****************************************************************************/
/*
 * Remove all of key & value pairs from this page to "recipient" page, then
 * update relavent key & value pair in its parent page.
 */
  INDEX_TEMPLATE_ARGUMENTS
  void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(
    BPlusTreeInternalPage *recipient, int index_in_parent,
    BufferPoolManager *buffer_pool_manager) {
    
    page_id_t parent_id = GetParentPageId();
    assert(parent_id == recipient->GetParentPageId());
    assert(parent_id != INVALID_PAGE_ID);
    assert(GetSize()>0 && recipient->GetSize()>0);

    auto parent_page = reinterpret_cast<BPlusTreeInternalPage*>(buffer_pool_manager->FetchPage(parent_id));
    int this_index = parent_page->ValueIndex(GetPageId());
    int recipient_index = parent_page->ValueIndex(recipient->GetPageId());

    // set a restriction that this_index is always bigger than recipient_index for simplicity
    assert(this_index != -1 && recipient_index != -1 && this_index > recipient_index);

    
    array[0].first = parent_page->KeyAt(this_index);
    recipient->CopyAllFrom(array, GetSize(), buffer_pool_manager);
    SetSize(0);

    // just for removing the warnings
    if (this_index > recipient_index) 
      parent_page->Remove(this_index);
    
    buffer_pool_manager->UnpinPage(parent_id, true);
  }

  INDEX_TEMPLATE_ARGUMENTS
  void B_PLUS_TREE_INTERNAL_PAGE_TYPE::CopyAllFrom(
    MappingType *items, int size, BufferPoolManager *buffer_pool_manager) {
    for (int i=0; i<size; i++) {
      auto child_page = reinterpret_cast<BPlusTreePage *>(buffer_pool_manager->FetchPage(items[i].second));
      child_page->SetParentPageId(GetPageId());
      buffer_pool_manager->UnpinPage(child_page->GetPageId(), true);
    }
    
    memmove(array+GetSize(), items, size*sizeof(MappingType));
    IncreaseSize(size); 
  }

/*****************************************************************************
 * REDISTRIBUTE
 *****************************************************************************/
/*
 * Remove the first key & value pair from this page to tail of "recipient"
 * page, then update relavent key & value pair in its parent page.
 */
  INDEX_TEMPLATE_ARGUMENTS
  void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstToEndOf(
    BPlusTreeInternalPage *recipient,
    BufferPoolManager *buffer_pool_manager) {
    page_id_t parent_id = GetParentPageId();
    assert(parent_id == recipient->GetParentPageId());
    assert(parent_id != INVALID_PAGE_ID);
    assert(GetSize()>0 && recipient->GetSize()>0);

    auto parent_page = reinterpret_cast<BPlusTreeInternalPage*>(buffer_pool_manager->FetchPage(parent_id));
    int this_index = parent_page->ValueIndex(GetPageId());
    int recipient_index = parent_page->ValueIndex(recipient->GetPageId());
    assert(this_index != -1 && recipient_index != -1 && this_index > recipient_index);
    
    CopyLastFrom({parent_page->KeyAt(this_index), array[0].second}, buffer_pool_manager);
    
    // just for removing the warnings
    if (this_index > recipient_index)
      parent_page->SetKeyAt(this_index, array[1].first);

    memmove(array, array+1, (GetSize()-1)*sizeof(MappingType));
    IncreaseSize(-1);
  }

  INDEX_TEMPLATE_ARGUMENTS
  void B_PLUS_TREE_INTERNAL_PAGE_TYPE::CopyLastFrom(
    const MappingType &pair, BufferPoolManager *buffer_pool_manager) {
    array[GetSize()] = pair;
    auto child_page = reinterpret_cast<BPlusTreePage *>(buffer_pool_manager->FetchPage(pair.second));
    child_page->SetParentPageId(GetPageId());
    IncreaseSize(1);
    buffer_pool_manager->UnpinPage(child_page->GetPageId(), true);
  }

/*
 * Remove the last key & value pair from this page to head of "recipient"
 * page, then update relavent key & value pair in its parent page.
 */
  INDEX_TEMPLATE_ARGUMENTS
  void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastToFrontOf(
    BPlusTreeInternalPage *recipient, int parent_index,
      BufferPoolManager *buffer_pool_manager) {
    page_id_t parent_id = GetParentPageId();
    assert(parent_id == recipient->GetParentPageId());
    assert(parent_id != INVALID_PAGE_ID);
    assert(GetSize()>0 && recipient->GetSize()>0);

    auto parent_page = reinterpret_cast<BPlusTreeInternalPage*>(buffer_pool_manager->FetchPage(parent_id));
    int this_index = parent_page->ValueIndex(GetPageId());
    int recipient_index = parent_page->ValueIndex(recipient->GetPageId());

    assert(this_index != -1 && recipient_index != -1);
    CopyFirstFrom({parent_page->KeyAt(this_index), array[GetSize()-1].second}, recipient_index, buffer_pool_manager);
    
    // just for removing the warnings
    if (this_index > recipient_index)
      parent_page->SetKeyAt(this_index, array[GetSize()-1].first);

    IncreaseSize(-1);
    buffer_pool_manager->UnpinPage(parent_id, true);
  }

  // why we need parent index?
  INDEX_TEMPLATE_ARGUMENTS
  void B_PLUS_TREE_INTERNAL_PAGE_TYPE::CopyFirstFrom(
    const MappingType &pair, int parent_index,
    BufferPoolManager *buffer_pool_manager) {
    
    memmove(array+1, array, GetSize()*sizeof(MappingType));
    array[1].first = pair.first;
    array[0].second = pair.second;
    auto child_page = reinterpret_cast<BPlusTreePage *>(buffer_pool_manager->FetchPage(pair.second));
    child_page->SetParentPageId(GetPageId());
    IncreaseSize(1);
    buffer_pool_manager->UnpinPage(child_page->GetPageId(), true);
  }

/*****************************************************************************
 * DEBUG
 *****************************************************************************/
  INDEX_TEMPLATE_ARGUMENTS
  void B_PLUS_TREE_INTERNAL_PAGE_TYPE::QueueUpChildren(
    std::queue<BPlusTreePage *> *queue,
    BufferPoolManager *buffer_pool_manager) {
    for (int i = 0; i < GetSize(); i++) {
      auto *page = buffer_pool_manager->FetchPage(array[i].second);
      if (page == nullptr)
	throw Exception(EXCEPTION_TYPE_INDEX,
			"all page are pinned while printing");
      BPlusTreePage *node =
        reinterpret_cast<BPlusTreePage *>(page->GetData());
      queue->push(node);
    }
  }

  INDEX_TEMPLATE_ARGUMENTS
  std::string B_PLUS_TREE_INTERNAL_PAGE_TYPE::ToString(bool verbose) const {
    if (GetSize() == 0) {
      return "";
    }
    std::ostringstream os;
    if (verbose) {
      os << "[pageId: " << GetPageId() << " parentId: " << GetParentPageId()
	 << "]<" << GetSize() << "> ";
    }

    int entry = verbose ? 0 : 1;
    int end = GetSize();
    bool first = true;
    while (entry < end) {
      if (first) {
	first = false;
      } else {
	os << " ";
      }
      os << std::dec << array[entry].first.ToString();
      if (verbose) {
	os << "(" << array[entry].second << ")";
      }
      ++entry;
    }
    return os.str();
  }

  INDEX_TEMPLATE_ARGUMENTS
  bool B_PLUS_TREE_INTERNAL_PAGE_TYPE::CheckIntegrity(const KeyType *lower_bound, const KeyType *higher_bound, const KeyComparator &comparator, BufferPoolManager* buffer_pool_manager) const {
    // for key[x] value[x] key[x+1]
    // for each K in value[x], key[x] <= K < key[x+1]
    KeyType prev_key = array[1].first;
    
    if (lower_bound != nullptr && comparator(*lower_bound, prev_key) > 0) {
      return false;
    }

    for (int i=2; i<GetSize(); i++) {
      if (comparator(prev_key, array[i].first) >= 0) {
	return false;
      }
      prev_key = array[i].first;
    }

    if (higher_bound != nullptr && comparator(prev_key, *higher_bound) >= 0) {
      return false;
    }

    if (!IsRootPage() && GetSize() < GetMaxSize() / 2) {
      return false;
    }

    return true;
  }

  // valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t,
				     GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t,
                                           GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t,
                                           GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t,
                                           GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t,
                                           GenericComparator<64>>;
} // namespace cmudb
