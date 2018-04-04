/**
 * b_plus_tree.cpp
 */
#include <iostream>
#include <string>

#include "common/exception.h"
#include "common/logger.h"
#include "common/rid.h"
#include "index/b_plus_tree.h"
#include "page/header_page.h"

namespace cmudb {

INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(const std::string &name,
                                BufferPoolManager *buffer_pool_manager,
                                const KeyComparator &comparator,
                                page_id_t root_page_id)
    : index_name_(name), root_page_id_(root_page_id),
      buffer_pool_manager_(buffer_pool_manager), comparator_(comparator) {}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
bool BPLUSTREE_TYPE::IsEmpty() const {
  if (root_page_id_ == INVALID_PAGE_ID) {
    return true;
  } 
  BPlusTreePage* page = reinterpret_cast<BPlusTreePage *>(buffer_pool_manager_->FetchPage(root_page_id_));
  buffer_pool_manager_->UnpinPage(root_page_id_, false);
  return page->GetSize() == 0; 
}
/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
bool BPLUSTREE_TYPE::GetValue(const KeyType &key,
                              std::vector<ValueType> &result,
                              Transaction *transaction) {
  BPlusTreePage* page = reinterpret_cast<BPlusTreePage *>(buffer_pool_manager_->FetchPage(root_page_id_));
  while (!page->IsLeafPage()) {
    auto internal_page = static_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(page);
    page_id_t page_id = internal_page->Lookup(key, comparator_);
    buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
    page = reinterpret_cast<BPlusTreePage *>(buffer_pool_manager_->FetchPage(page_id)); 
  }
  auto leaf_page = static_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(page);

  ValueType value; 
  if (leaf_page->Lookup(key, value, comparator_)) {
    result.push_back(value);
    return true;
  }
  return false;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
bool BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value,
                            Transaction *transaction) {
  if (root_page_id_ == INVALID_PAGE_ID) {
    StartNewTree(key, value);
    return true;
  }
  
  return InsertIntoLeaf(key, value);
}
/*
 * Insert constant key & value pair into an empty tree
 * User needs to first ask for new page from buffer pool manager(NOTICE: throw
 * an "out of memory" exception if returned value is nullptr), then update b+
 * tree's root page id and insert entry directly into leaf page.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::StartNewTree(const KeyType &key, const ValueType &value) {
  page_id_t page_id;
  auto page = buffer_pool_manager_->NewPage(page_id);
  if (page == nullptr) {
    throw "out of memory";
  } 
  root_page_id_ = page_id;
  UpdateRootPageId();
  auto leaf_page = reinterpret_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(page);
  leaf_page->Init(page_id, INVALID_PAGE_ID); 
  leaf_page->Insert(key, value, comparator_); 
  buffer_pool_manager_->UnpinPage(page_id, true);
}

/*
 * Insert constant key & value pair into leaf page
 * User needs to first find the right leaf page as insertion target, then look
 * through leaf page to see whether insert key exist or not. If exist, return
 * immdiately, otherwise insert entry. Remember to deal with split if necessary.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
bool BPLUSTREE_TYPE::InsertIntoLeaf(const KeyType &key, const ValueType &value,
                                    Transaction *transaction) {
  BPlusTreePage* page = reinterpret_cast<BPlusTreePage *>(buffer_pool_manager_->FetchPage(root_page_id_));
  while (!page->IsLeafPage()) {
    auto internal_page = static_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(page);
    page_id_t page_id = internal_page->Lookup(key, comparator_);
    // TODO
    //   we should use vector, and unpin them after all
    buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
    page = reinterpret_cast<BPlusTreePage *>(buffer_pool_manager_->FetchPage(page_id));
  }
  auto leaf_page = static_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(page);
  ValueType old_value;
  if (leaf_page->Lookup(key, old_value, comparator_)) {
    return false;
  }
  
  if (leaf_page->GetSize()+1 > leaf_page->GetMaxSize()) {
    auto new_page = Split(leaf_page); 
    auto pop_key = new_page->KeyAt(0); 
    if (comparator_(key, pop_key) < 0) {
      leaf_page->Insert(key, value, comparator_); 
    } else {
      new_page->Insert(key, value, comparator_); 
    }
    InsertIntoParent(leaf_page, pop_key, new_page);
    buffer_pool_manager_->UnpinPage(new_page->GetPageId(), true);
  } else {
    leaf_page->Insert(key, value, comparator_);
  }

  buffer_pool_manager_->UnpinPage(leaf_page->GetPageId(), true);
  return true;
}

/*
 * Split input page and return newly created page.
 * Using template N to represent either internal page or leaf page.
 * User needs to first ask for new page from buffer pool manager(NOTICE: throw
 * an "out of memory" exception if returned value is nullptr), then move half
 * of key & value pairs from input page to newly created page
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename N> N *BPLUSTREE_TYPE::Split(N *node) {
  page_id_t new_page_id;
  auto new_page = reinterpret_cast<N*>(buffer_pool_manager_->NewPage(new_page_id));
  new_page->Init(new_page_id, node->GetParentPageId());
  if (new_page == nullptr) {
    throw "out of memory";
  }

  node->MoveHalfTo(new_page, buffer_pool_manager_);

  // This api seems awkward to me
  // TODO
  // should I do this
  // buffer_pool_manager_->UnpinPage(new_page_id, true);
  return new_page;
}

/*
 * Insert key & value pair into internal page after split
 * @param   old_node      input page from split() method
 * @param   key
 * @param   new_node      returned page from split() method
 * User needs to first find the parent page of old_node, parent node must be
 * adjusted to take info of new_node into account. Remember to deal with split
 * recursively if necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertIntoParent(BPlusTreePage *old_node,
                                      const KeyType &key,
                                      BPlusTreePage *new_node,
                                      Transaction *transaction) {
  page_id_t parent_page_id = old_node->GetParentPageId();
    
  if (parent_page_id == INVALID_PAGE_ID) {
    // populate the new root if the root page is overflow
    auto parent_page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(buffer_pool_manager_->NewPage(parent_page_id));
    if (parent_page == nullptr)
      throw "out of memory";
  
    parent_page->Init(parent_page_id, INVALID_PAGE_ID); 
    parent_page->PopulateNewRoot(old_node->GetPageId(), key, new_node->GetPageId());
    old_node->SetParentPageId(parent_page_id);
    new_node->SetParentPageId(parent_page_id);
    root_page_id_ = parent_page_id;
    UpdateRootPageId(); 
  } else {
    auto parent_page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(buffer_pool_manager_->FetchPage(parent_page_id));
    if (parent_page->GetSize() + 1 > parent_page->GetMaxSize()) {
      // if the parent page is overflow, we should recursively split
      auto new_page = Split(parent_page);
      // important and beautiful idea
      auto pop_key = new_page->KeyAt(0); 
      if (comparator_(key, pop_key) < 0) {
	parent_page->InsertNodeAfter(old_node->GetPageId(), key, new_node->GetPageId());
	new_node->SetParentPageId(parent_page->GetPageId());
      } else {
	new_page->InsertNodeAfter(old_node->GetPageId(), key, new_node->GetPageId());
	new_node->SetParentPageId(new_page->GetPageId());
      }
      InsertIntoParent(parent_page, pop_key, new_page);
      buffer_pool_manager_->UnpinPage(new_page->GetPageId(), true);
    } else {
      // just insert and return
      // auto page = reinterpret_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(buffer_pool_manager_->FetchPage(1)); 
      // std::cout << page->ToString(true) << std::endl;

      // auto page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(buffer_pool_manager_->FetchPage(887)); 
      // std::cout << page->ToString(true) << std::endl;
      parent_page->InsertNodeAfter(old_node->GetPageId(), key, new_node->GetPageId());
      
    }  
  }
  buffer_pool_manager_->UnpinPage(parent_page_id, true);
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immdiately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key, Transaction *transaction) {
  if (IsEmpty()) {
    return ;
  }
  
}

/*
 * User needs to first find the sibling of input page. If sibling's size + input
 * page's size > page's max size, then redistribute. Otherwise, merge.
 * Using template N to represent either internal page or leaf page.
 * @return: true means target leaf page should be deleted, false means no
 * deletion happens
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename N>
bool BPLUSTREE_TYPE::CoalesceOrRedistribute(N *node, Transaction *transaction) {
  page_id_t parent_page_id = node->GetParentPageId();
  assert(parent_page_id != INVALID_PAGE_ID);

  auto parent_page = reinterpret_cast<BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>*>(buffer_pool_manager_->FetchPage(parent_page_id));
  int node_index = parent_page->ValueIndex(node);
  N *left_sibling_page, right_sibling_page;
  
  if (node_index-1 >= 0) {
    int left_sibling_page_id = parent_page->ValueAt(node_index-1);
    left_sibling_page = reinterpret_cast<N*>(buffer_pool_manager_->FetchPage(left_sibling_page_id));
    if (left_sibling_page->GetSize()+node->GetSize() > node->GetMaxSize()) {
      return Redistribute(left_sibling_page, node, node_index);
    }
  }

  if (node_index+1 < parent_page->GetSize()) {
    int right_sibling_page_id = parent_page->ValueAt(node_index+1);
    right_sibling_page = reinterpret_cast<N*>(buffer_pool_manager_->FetchPage(right_sibling_page_id));
    if (right_sibling_page->GetSize()+node->GetSize() > node->GetMaxSize()) {
      return Redistribute(right_sibling_page, node, node_index); 
    } 
  }

  if (node_index -1>=0)
    return Coalesce(left_sibling_page, node, parent_page, node_index);
  else
    return Coalesce(right_sibling_page, node, parent_page, node_index);
  
  return false;
}

/*
 * Move all the key & value pairs from one page to its sibling page, and notify
 * buffer pool manager to delete this page. Parent page must be adjusted to
 * take info of deletion into account. Remember to deal with coalesce or
 * redistribute recursively if necessary.
 * Using template N to represent either internal page or leaf page.
 * @param   neighbor_node      sibling page of input "node"
 * @param   node               input from method coalesceOrRedistribute()
 * @param   parent             parent page of input "node"
 * @return  true means parent node should be deleted, false means no deletion
 * happend
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename N>
bool BPLUSTREE_TYPE::Coalesce(
    N *&neighbor_node, N *&node,
    BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *&parent,
    int index, Transaction *transaction) {

  

  
  return false;
}

/*
 * Redistribute key & value pairs from one page to its sibling page. If index ==
 * 0, move sibling page's first key & value pair into end of input "node",
 * otherwise move sibling page's last key & value pair into head of input
 * "node".
 * Using template N to represent either internal page or leaf page.
 * @param   neighbor_node      sibling page of input "node"
 * @param   node               input from method coalesceOrRedistribute()
 */
INDEX_TEMPLATE_ARGUMENTS
template <typename N>
void BPLUSTREE_TYPE::Redistribute(N *neighbor_node, N *node, int index) {}
/*
 * Update root page if necessary
 * NOTE: size of root page can be less than min size and this method is only
 * called within coalesceOrRedistribute() method
 * case 1: when you delete the last element in root page, but root page still
 * has one last child
 * case 2: when you delete the last element in whole b+ tree
 * @return : true means root page should be deleted, false means no deletion
 * happend
 */
INDEX_TEMPLATE_ARGUMENTS
bool BPLUSTREE_TYPE::AdjustRoot(BPlusTreePage *old_root_node) {
  return false;
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the leaftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE BPLUSTREE_TYPE::Begin() { return INDEXITERATOR_TYPE(); }

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE BPLUSTREE_TYPE::Begin(const KeyType &key) {
  return INDEXITERATOR_TYPE();
}

/*****************************************************************************
 * UTILITIES AND DEBUG
 *****************************************************************************/
/*
 * Find leaf page containing particular key, if leftMost flag == true, find
 * the left most leaf page
 */
INDEX_TEMPLATE_ARGUMENTS
B_PLUS_TREE_LEAF_PAGE_TYPE *BPLUSTREE_TYPE::FindLeafPage(const KeyType &key,
                                                         bool leftMost) {
  return nullptr;
}

/*
 * Update/Insert root page id in header page(where page_id = 0, header_page is
 * defined under include/page/header_page.h)
 * Call this method everytime root page id is changed.
 * @parameter: insert_record      defualt value is false. When set to true,
 * insert a record <index_name, root_page_id> into header page instead of
 * updating it.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::UpdateRootPageId(int insert_record) {
  HeaderPage *header_page = static_cast<HeaderPage *>(
      buffer_pool_manager_->FetchPage(HEADER_PAGE_ID));
  if (insert_record)
    // create a new record<index_name + root_page_id> in header_page
    header_page->InsertRecord(index_name_, root_page_id_);
  else
    // update root_page_id in header_page
    header_page->UpdateRecord(index_name_, root_page_id_);
  buffer_pool_manager_->UnpinPage(HEADER_PAGE_ID, true);
}

/*
 * This method is used for debug only
 * print out whole b+tree sturcture, rank by rank
 */
INDEX_TEMPLATE_ARGUMENTS
std::string BPLUSTREE_TYPE::ToString(bool verbose) { return "Empty tree"; }

  
  INDEX_TEMPLATE_ARGUMENTS
  void BPLUSTREE_TYPE::SayPage(int page_id, int type) {
    if (type == 1) {
      auto page = reinterpret_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(buffer_pool_manager_->FetchPage(page_id)); 
      std::cout << page->ToString(true) << std::endl;
    } else {
      auto page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(buffer_pool_manager_->FetchPage(page_id)); 
      std::cout << page->ToString(true) << std::endl;
      std::cout << "*"<< page->IsLeafPage() << std::endl;
    } 
  }
  

  /*
   * This method is used for test only
   * Read data from file and insert one by one
   */
  INDEX_TEMPLATE_ARGUMENTS
  void BPLUSTREE_TYPE::InsertFromFile(const std::string &file_name,
				      Transaction *transaction) {
    int64_t key;
    std::ifstream input(file_name);
    while (input) {
      input >> key;

      KeyType index_key;
      index_key.SetFromInteger(key);
      RID rid(key);
      Insert(index_key, rid, transaction);
    }
  }
/*
 * This method is used for test only
 * Read data from file and remove one by one
 */
  INDEX_TEMPLATE_ARGUMENTS
  void BPLUSTREE_TYPE::RemoveFromFile(const std::string &file_name,
				      Transaction *transaction) {
    int64_t key;
    std::ifstream input(file_name);
    while (input) {
      input >> key;
      KeyType index_key;
      index_key.SetFromInteger(key);
      Remove(index_key, transaction);
    }
  }

  template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

} // namespace cmudb
