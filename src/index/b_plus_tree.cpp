/**
 * b_plus_tree.cpp
 */
#include <iostream>
#include <string>
#include <queue>

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
      buffer_pool_manager_->UnpinPage(internal_page->GetPageId(), false); 
      page = reinterpret_cast<BPlusTreePage *>(buffer_pool_manager_->FetchPage(page_id));         
    }
    auto leaf_page = static_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(page);
  
    ValueType value; 
    if (leaf_page->Lookup(key, value, comparator_)) {
      result.push_back(value);
      buffer_pool_manager_->UnpinPage(leaf_page->GetPageId(), false);
      return true;
    }
    buffer_pool_manager_->UnpinPage(leaf_page->GetPageId(), false); 
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

  // TODO(Handora): use static_cast instead of reinterpret_cast
  auto page = reinterpret_cast<BPlusTreePage*>(buffer_pool_manager_->FetchPage(root_page_id_));
  while (!page->IsLeafPage()) {
    auto internal_page = static_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(page);
    page_id_t page_id = internal_page->Lookup(key, comparator_);
    buffer_pool_manager_->UnpinPage(internal_page->GetPageId(), false);
    page = reinterpret_cast<BPlusTreePage*>(buffer_pool_manager_->FetchPage(page_id));
  }
  auto leaf_page = static_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(page);
  
  leaf_page->RemoveAndDeleteRecord(key, comparator_); 
  
  // if page is the root
  if (page->IsRootPage()) {
    if (!AdjustRoot(page)) {
      buffer_pool_manager_->UnpinPage(leaf_page->GetPageId(), true); 
    } else {
      buffer_pool_manager_->UnpinPage(leaf_page->GetPageId(), true);
      buffer_pool_manager_->DeletePage(leaf_page->GetPageId());
    }
    return ;
  }

  bool ok = false;
  if (leaf_page->GetSize() < leaf_page->GetMinSize()) {
    ok = CoalesceOrRedistribute(leaf_page);
  } 
  buffer_pool_manager_->UnpinPage(leaf_page->GetPageId(), true);
  if (ok) {
    assert(buffer_pool_manager_->DeletePage(leaf_page->GetPageId()));
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
  if (node->GetSize() >= node->GetMinSize()) {
    return false;
  }

  std::cout << "==" << parent_page_id << std::endl;
  
  auto parent_page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(buffer_pool_manager_->FetchPage(parent_page_id));

  std::cout << "Hello" << std::endl;
  for (auto v: buffer_pool_manager_->PinnedPageId()) {
    std::cout << v << std::endl;
  }
  std::cout << "Hello" << std::endl;
 
  int node_index = parent_page->ValueIndex(node->GetPageId());
  N *left_sibling_page = nullptr, *right_sibling_page = nullptr;
  
  if (node_index-1 >= 0) {
    page_id_t left_sibling_page_id = parent_page->ValueAt(node_index-1);
    left_sibling_page = reinterpret_cast<N*>(buffer_pool_manager_->FetchPage(left_sibling_page_id));

    // because we already delete the one, so we can merge it when sum size is maxsize
    if (left_sibling_page->GetSize()+node->GetSize() >= node->GetMaxSize()) {
      // move from the left sibling page to the node so the index is
      // not zero
      Redistribute(left_sibling_page, node, 1);
      buffer_pool_manager_->UnpinPage(left_sibling_page->GetPageId(), true);
      buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
      return false;
    }
  }

  // because we already delete the one, so we can merge it when sum size is maxsize
  if (node_index+1 < parent_page->GetSize()) {
    page_id_t right_sibling_page_id = parent_page->ValueAt(node_index+1);
    right_sibling_page = reinterpret_cast<N*>(buffer_pool_manager_->FetchPage(right_sibling_page_id));
    if (right_sibling_page->GetSize()+node->GetSize() >= node->GetMaxSize()) {
      // move from the right sibling page to the node so the index is
      // zero
      Redistribute(right_sibling_page, node, 0);
      if (left_sibling_page != nullptr) {
	buffer_pool_manager_->UnpinPage(left_sibling_page->GetPageId(), true);
      }
      buffer_pool_manager_->UnpinPage(right_sibling_page->GetPageId(), true);
      buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
      return false;
    } 
  }

  bool ok = false;
  bool delete_node = false;
  
  if (node_index - 1 >= 0) {
    ok = Coalesce(left_sibling_page, node, parent_page, node_index);
    delete_node = true;
  } else {
    ok = Coalesce(node, right_sibling_page, parent_page, node_index);
    delete_node = false;
    buffer_pool_manager_->UnpinPage(right_sibling_page->GetPageId(), true);
    assert(buffer_pool_manager_->DeletePage(right_sibling_page->GetPageId()));
    right_sibling_page = nullptr;
  }

  // TODO(Handora): optimization
  // may be left or right or parent not changed
  if (left_sibling_page != nullptr) {
    buffer_pool_manager_->UnpinPage(left_sibling_page->GetPageId(), true);
  } 

  if (right_sibling_page != nullptr) {
    buffer_pool_manager_->UnpinPage(right_sibling_page->GetPageId(), true);
  }

  // this sstyle is ugly
  // but we need do so to make it simpler
  // may be need some optimization
  if (ok) {
    buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
    assert(buffer_pool_manager_->DeletePage(parent_page->GetPageId()));
  } else {
    buffer_pool_manager_->UnpinPage(parent_page->GetPageId(), true);
  }

  return delete_node;
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
  
  // TODO(Handora): The second parameter is now no use
  node->MoveAllTo(neighbor_node, index, buffer_pool_manager_); 

  if (parent->IsRootPage()) {
    return AdjustRoot(parent); 
  } 
  return CoalesceOrRedistribute(parent);
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
void BPLUSTREE_TYPE::Redistribute(N *neighbor_node, N *node, int index) {
  if (index == 0) {
    neighbor_node->MoveFirstToEndOf(node, buffer_pool_manager_); 
  } else {
    // the second parameter seems no use
    neighbor_node->MoveLastToFrontOf(node, index, buffer_pool_manager_);
  }
}
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
  if (old_root_node->GetSize() >= 2) {
    return false;
  } else if (old_root_node->GetSize() == 1) {
    if (old_root_node->IsLeafPage()) {
      root_page_id_ = INVALID_PAGE_ID;
    } else {
      auto internal_page = static_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(old_root_node);
      root_page_id_ = internal_page->ValueAt(0);
    }
    
    UpdateRootPageId(); 
    return true;
  }

  // never reach here
  // for warning purpose
  assert(false);
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
std::string BPLUSTREE_TYPE::ToString(bool verbose) {
  std::ostringstream os;
  if (IsEmpty()) {
    return "Empty tree";
  }

  auto page_queue = new std::queue<std::pair<page_id_t, int>>;

  page_queue->push({root_page_id_, 1});

  int phase = 0;
  
  while (!page_queue->empty()) {
    auto t = page_queue->front();
    page_queue->pop();

    auto page = reinterpret_cast<BPlusTreePage*>(buffer_pool_manager_->FetchPage(t.first));
    if (page == nullptr) {
      LOG_DEBUG("Buffer Pool may be full"); 
    }

    if (phase != t.second) {
      os << "=============================================" << std::endl;
      os << "The " << t.second << " Phase:" << std::endl;
      phase = t.second;
    }

    if (page->IsLeafPage()) {
      auto leaf_page = reinterpret_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(page);
      os << leaf_page->ToString(verbose) << std::endl;
    } else {
      auto internal_page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(page);
      os << internal_page->ToString(verbose) << std::endl;
    }
    
    if (!page->IsLeafPage()) {
      for (int i=0; i<page->GetSize(); i++) {
	auto internal_page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(page);
	page_queue->push({internal_page->ValueAt(i), t.second+1}); 
      }
    }

    buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
  }

  return os.str();
}


  /*****************************************************************************
   * TEST ONLY
   *****************************************************************************/
  
  INDEX_TEMPLATE_ARGUMENTS
  void BPLUSTREE_TYPE::SayPage(int page_id) {
    auto page = reinterpret_cast<BPlusTreePage*>(buffer_pool_manager_->FetchPage(page_id));
    int type = page->IsLeafPage();
    if (type == 1) {
      auto leaf_page = reinterpret_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(page);
      std::cout << "Leaf Page: ";
      std::cout << leaf_page->ToString(true) << std::endl;
    } else {
      auto internal_page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(page);
      std::cout << "Internal Page: ";
      std::cout << internal_page->ToString(true) << std::endl; 
    }
    buffer_pool_manager_->UnpinPage(page_id, false);
  }

  INDEX_TEMPLATE_ARGUMENTS
  bool BPLUSTREE_TYPE::CheckIntegrity() const {
    if (root_page_id_ == INVALID_PAGE_ID) {
      // we should prove that
      // tree may be not empty tree
      return true;
    }

    auto page_queue = new std::queue<std::tuple<page_id_t, std::shared_ptr<KeyType>, std::shared_ptr<KeyType>>>;

    page_queue->push({root_page_id_, nullptr, nullptr});

    while (!page_queue->empty()) {
      auto t = page_queue->front();
      page_queue->pop();

      auto page = reinterpret_cast<BPlusTreePage*>(buffer_pool_manager_->FetchPage(std::get<0>(t)));
      if (page == nullptr) {
	LOG_DEBUG("Buffer Pool may be full");
	return false;
      }
      
      bool ok;
      if (page->IsLeafPage()) {
	auto leaf_page = reinterpret_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(page);
	ok = leaf_page->CheckIntegrity(std::get<1>(t), std::get<2>(t), comparator_, buffer_pool_manager_);
      } else {
	auto internal_page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(page);
	ok = internal_page->CheckIntegrity(std::get<1>(t), std::get<2>(t), comparator_, buffer_pool_manager_);
      }
      if (ok == false) {
	buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
	LOG_DEBUG("Page %d Intergrity error\n", page->GetPageId());
	if (page->IsLeafPage()) {
	  auto leaf_page = reinterpret_cast<B_PLUS_TREE_LEAF_PAGE_TYPE*>(page);
	  if (leaf_page)
	    LOG_DEBUG("%s\n", leaf_page->ToString(true).c_str());
	} else {
	  auto internal_page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(page);
          if (internal_page)
	    LOG_DEBUG("%s\n", internal_page->ToString(true).c_str());
	}
        
	return false;
      }

      if (!page->IsLeafPage()) {
	for (int i=0; i<page->GetSize(); i++) {
	  auto internal_page = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>*>(page);

	  if (i == 0) {
	    page_queue->push({internal_page->ValueAt(i), std::get<1>(t), std::make_shared<KeyType>(internal_page->KeyAt(i+1))}); 
	  } else if (i == page->GetSize()-1) {
	    page_queue->push({internal_page->ValueAt(i), std::make_shared<KeyType>(internal_page->KeyAt(i)), std::get<2>(t)}); 
	  } else {
	    page_queue->push({internal_page->ValueAt(i), std::make_shared<KeyType>(internal_page->KeyAt(i)), std::make_shared<KeyType>(internal_page->KeyAt(i+1))}); 
	  }
	}
      }

      buffer_pool_manager_->UnpinPage(page->GetPageId(), false);
    }
    
    return true;
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
