/**
 * b_plus_tree_leaf_page.cpp
 */

#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "common/logger.h"
#include "page/b_plus_tree_leaf_page.h"
#include "page/b_plus_tree_internal_page.h"

namespace cmudb {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * Init method after creating a new leaf page
 * Including set page type, set current size to zero, set page id/parent id, set
 * next page id and set max size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(page_id_t page_id, page_id_t parent_id) {
  SetPageId(page_id);
  SetParentPageId(parent_id);
  SetSize(0);
  SetPageType(IndexPageType::LEAF_PAGE);
  next_page_id_ = INVALID_PAGE_ID;
  SetMaxSize((PAGE_SIZE - sizeof(BPlusTreeLeafPage)) / (sizeof(KeyType) + sizeof(ValueType)));
}

/**
 * Helper methods to set/get next page id
 */
INDEX_TEMPLATE_ARGUMENTS
page_id_t B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const {
  return next_page_id_;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) {
  next_page_id_ = next_page_id;
}

/**
 * Helper method to find the first index i so that array[i].first >= key
 * NOTE: This method is only used when generating index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_LEAF_PAGE_TYPE::KeyIndex(
    const KeyType &key, const KeyComparator &comparator) const {

  for (int i = 0; i < GetSize(); ++i) {
	if (comparator(array[i].first, key) >= 0) {
	  return i;
	}
  }
  return -1;
}

/*
 * Helper method to find and return the key associated with input "index"(a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
KeyType B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const {
  // replace with your own code
  assert(index >= 0 && index < GetSize());

  return array[index].first;
}

/*
 * Helper method to find and return the key & value pair associated with input
 * "index"(a.k.a array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
const MappingType &B_PLUS_TREE_LEAF_PAGE_TYPE::GetItem(int index) {
  // replace with your own code
  assert(index >= 0 && index < GetSize());

  return array[index];
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert key & value pair into leaf page ordered by key
 * @return  page size after insertion
 */
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(const KeyType &key,
									   const ValueType &value,
									   const KeyComparator &comparator) {
  assert(GetSize() < GetMaxSize());

  int key_index = KeyIndex(key, comparator);

  // the index only support unique key
  if (key_index == -1) {
	key_index = GetSize();
  } else {
	if (comparator(KeyAt(key_index), key) == 0) {
	  return GetSize();
	}
	memmove(array + key_index + 1, array + key_index, (GetSize() - key_index) * sizeof(MappingType));
  }

  array[key_index] = {key, value};
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
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveHalfTo(
	BPlusTreeLeafPage *recipient,
	BufferPoolManager *) {

  int origin_size = GetSize();
  SetSize(GetSize() / 2);
  recipient->CopyHalfFrom(array + GetSize(), origin_size - GetSize());
  recipient->SetNextPageId(GetNextPageId());
  SetNextPageId(recipient->GetPageId());
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::CopyHalfFrom(MappingType *items, int size) {
  // move to a brand-new page
  memmove(array, items, size * sizeof(MappingType));
  IncreaseSize(size);
}

/*****************************************************************************
 * LOOKUP
 *****************************************************************************/
/*
 * For the given key, check to see whether it exists in the leaf page. If it
 * does, then store its corresponding value in input "value" and return true.
 * If the key does not exist, then return false
 */
INDEX_TEMPLATE_ARGUMENTS
bool B_PLUS_TREE_LEAF_PAGE_TYPE::Lookup(const KeyType &key, ValueType &value,
										const KeyComparator &comparator) const {
  for (int i = 0; i < GetSize(); ++i) {

	if (comparator(key, array[i].first) == 0) {
	  value = array[i].second;
	  return true;
	}
  }

  return false;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/**
 * First look through leaf page to see whether delete key exist or not. If
 * exist, perform deletion, otherwise return immdiately.
 * NOTE: store key&value pair continuously after deletion
 * @return   page size after deletion
 */
INDEX_TEMPLATE_ARGUMENTS
int B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveAndDeleteRecord(
    const KeyType &key, const KeyComparator &comparator) {

  if (!IsRootPage())
	assert(GetSize() >= GetMinSize());

  for (int i = 0; i < GetSize(); ++i) {
	if (comparator(key, array[i].first) == 0) {
	  memmove(array + i, array + i + 1, (GetSize() - i - 1) * sizeof(MappingType));
	  IncreaseSize(-1);
	  break;
	}
  }
  return GetSize();
}

/*****************************************************************************
 * MERGE
 *****************************************************************************/
/*
 * Remove all of key & value pairs from this page to "recipient" page, then
 * update next page id
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveAllTo(BPlusTreeLeafPage *recipient,
										   int, BufferPoolManager *buffer_pool_manager) {
  // give a restriction that always move from bigger to little
  // TODO
  // should this restriction be relaxed?

  page_id_t parent_id = GetParentPageId();
  assert(parent_id == recipient->GetParentPageId());
  assert(parent_id != INVALID_PAGE_ID);

  // TODO(Handora): This should be moved to outside
  auto parent_page =
	  reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(buffer_pool_manager->FetchPage(
		  parent_id)->GetData());
  assert(parent_page != nullptr);

  int index = parent_page->ValueIndex(GetPageId());
  parent_page->Remove(index);
  buffer_pool_manager->UnpinPage(parent_id, true);

  recipient->CopyAllFrom(array, GetSize());
  recipient->SetNextPageId(GetNextPageId());
  SetNextPageId(INVALID_PAGE_ID);
  SetSize(0);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::CopyAllFrom(MappingType *items, int size) {

  memmove(array + GetSize(), items, size * sizeof(MappingType));
  IncreaseSize(size);
}

/*****************************************************************************
 * REDISTRIBUTE
 *****************************************************************************/
/*
 * Remove the first key & value pair from this page to "recipient" page, then
 * update relavent key & value pair in its parent page.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstToEndOf(
    BPlusTreeLeafPage *recipient,
    BufferPoolManager *buffer_pool_manager) {

  page_id_t parent_id = GetParentPageId();
  assert(parent_id == recipient->GetParentPageId());
  assert(parent_id != INVALID_PAGE_ID);

  recipient->CopyLastFrom(array[0]);
  memmove(array, array + 1, (GetSize() - 1) * sizeof(MappingType));
  IncreaseSize(-1);

  auto parent_page =
	  reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(buffer_pool_manager->FetchPage(
		  parent_id)->GetData());
  int index = parent_page->ValueIndex(GetPageId());
  parent_page->SetKeyAt(index, array[0].first);
  buffer_pool_manager->UnpinPage(parent_id, true);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::CopyLastFrom(const MappingType &item) {
  array[GetSize()] = item;
  IncreaseSize(1);
}
/*
 * Remove the last key & value pair from this page to "recipient" page, then
 * update relavent key & value pair in its parent page.
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastToFrontOf(
    BPlusTreeLeafPage *recipient, int parentIndex,
    BufferPoolManager *buffer_pool_manager) {

  page_id_t parent_id = GetParentPageId();
  assert(parent_id == recipient->GetParentPageId());
  assert(parent_id != INVALID_PAGE_ID);
  auto parent_page =
	  reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(buffer_pool_manager->FetchPage(
		  parent_id)->GetData());
  int index = parent_page->ValueIndex(recipient->GetPageId());
  recipient->CopyFirstFrom(array[GetSize() - 1], index, buffer_pool_manager);
  IncreaseSize(-1);
  buffer_pool_manager->UnpinPage(parent_id, true);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::CopyFirstFrom(
    const MappingType &item, int parentIndex,
    BufferPoolManager *buffer_pool_manager) {

  page_id_t parent_id = GetParentPageId();
  assert(parent_id != INVALID_PAGE_ID);

  // move the item to the first of the array and increase the size
  memmove(array + 1, array, GetSize() * sizeof(MappingType));
  array[0] = item;
  IncreaseSize(1);

  auto parent_page =
	  reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(buffer_pool_manager->FetchPage(
		  parent_id)->GetData());
  parent_page->SetKeyAt(parentIndex, array[0].first);
  buffer_pool_manager->UnpinPage(parent_id, true);
}

/*****************************************************************************
 * DEBUG
 *****************************************************************************/
INDEX_TEMPLATE_ARGUMENTS
std::string B_PLUS_TREE_LEAF_PAGE_TYPE::ToString(bool verbose) const {
  if (GetSize() == 0) {
	return "";
  }

  std::ostringstream stream;
  if (verbose) {
	stream << "[pageId: " << GetPageId() << " parentId: " << GetParentPageId()
		   << "]<" << GetSize() << "> ";
  }
  int entry = 0;
  int end = GetSize();
  bool first = true;

  while (entry < end) {
	if (first) {
	  first = false;
	} else {
	  stream << " ";
	}
	stream << std::dec << array[entry].first;
	if (verbose) {
	  stream << "(" << array[entry].second << ")";
	}
	++entry;
  }
  return stream.str();
}

/**
 * test purpose
 */
INDEX_TEMPLATE_ARGUMENTS
bool B_PLUS_TREE_LEAF_PAGE_TYPE::CheckIntegrity(std::shared_ptr<KeyType> lower_bound,
												std::shared_ptr<KeyType> higher_bound,
												const KeyComparator &comparator,
												BufferPoolManager *buffer_pool_manager) const {
  // for key[x] value[x] key[x+1]
  // for each K in value[x], key[x] <= K < key[x+1]
  KeyType prev_key = array[1].first;

  // 1. all keys in the page is larger than the lower bounder
  if (lower_bound != nullptr && comparator(*lower_bound, prev_key) > 0) {
	LOG_DEBUG("lower than lower bound");
	return false;
  }

  // 2. keys is in sorted order
  for (int i = 2; i < GetSize(); i++) {
	if (comparator(prev_key, array[i].first) >= 0) {
	  LOG_DEBUG("lower than the prev one");
	  return false;
	}
	prev_key = array[i].first;
  }

  // 3. all keys in the page is smaller than the higher bounder
  if (higher_bound != nullptr && comparator(prev_key, *higher_bound) >= 0) {
	LOG_DEBUG("higher than higher bound");
	return false;
  }

  // 4. check the size constraints
  if (!IsRootPage() && GetSize() < GetMaxSize() / 2) {
	LOG_DEBUG("lower than half size");
	return false;
  }

  return true;
}

template
class BPlusTreeLeafPage<GenericKey<4>, RID,
						GenericComparator<4>>;
template
class BPlusTreeLeafPage<GenericKey<8>, RID,
						GenericComparator<8>>;
template
class BPlusTreeLeafPage<GenericKey<16>, RID,
						GenericComparator<16>>;
template
class BPlusTreeLeafPage<GenericKey<32>, RID,
						GenericComparator<32>>;
template
class BPlusTreeLeafPage<GenericKey<64>, RID,
						GenericComparator<64>>;
} // namespace cmudb
