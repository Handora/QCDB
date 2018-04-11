#include "buffer/buffer_pool_manager.h"

namespace cmudb {

  /*
   * BufferPoolManager Constructor
   * When log_manager is nullptr, logging is disabled (for test purpose)
   * WARNING: Do Not Edit This Function
   */
  BufferPoolManager::BufferPoolManager(size_t pool_size,
				       DiskManager *disk_manager,
				       LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager),
      log_manager_(log_manager) {
  // a consecutive memory space for buffer pool
  pages_ = new Page[pool_size_];
  page_table_ = new ExtendibleHash<page_id_t, Page *>(BUCKET_SIZE);
  replacer_ = new LRUReplacer<Page *>;
  free_list_ = new std::list<Page *>;

  // put all the pages into free list
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_->push_back(&pages_[i]); 
  }
}

/*
 * BufferPoolManager Deconstructor
 * WARNING: Do Not Edit This Function
 */
BufferPoolManager::~BufferPoolManager() {
  delete[] pages_;
  delete page_table_;
  delete replacer_;
  delete free_list_;
}

/**
 * 1. search hash table.
 *  1.1 if exist, pin the page and return immediately
 *  1.2 if no exist, find a replacement entry from either free list or lru
 *      replacer. (NOTE: always find from free list first)
 * 2. If the entry chosen for replacement is dirty, write it back to disk.
 * 3. Delete the entry for the old page from the hash table and insert an
 * entry for the new page.
 * 4. Update page metadata, read page content from disk file and return page
 * pointer
 */
Page *BufferPoolManager::FetchPage(page_id_t page_id) {
  assert(page_id != INVALID_PAGE_ID);
  std::lock_guard<std::mutex> latch(latch_);
  Page* page = nullptr;
  bool ok = page_table_->Find(page_id, page); 
  
  // if exist, pin the page and return immediately
  if (ok) {
    if (page->pin_count_ == 0)
      replacer_->Erase(page); 
    page->pin_count_ += 1; 
    return page;
  }
  
  if (!free_list_->empty()) {
    // find from free list
    page = free_list_->front();
    free_list_->pop_front();
  } else {
    // find from lru replacer 
    bool ok = replacer_->Victim(page);
    if (!ok) {
      // all page are pinned 
      return nullptr;
    }
    assert(page_table_->Remove(page->page_id_)); 

    if (page->is_dirty_) {
      disk_manager_->WritePage(page->page_id_, page->data_);
    }
  }
  
  page->is_dirty_ = false; 
  page->pin_count_ = 1;
  page->page_id_ = page_id;
  disk_manager_->ReadPage(page_id, page->data_);
						      
  page_table_->Insert(page_id, page); 
  return page;
}

/*
 * Implementation of unpin page
 * if pin_count>0, decrement it and if it becomes zero, put it back to
 * replacer if pin_count<=0 before this call, return false. is_dirty: set the
 * dirty flag of this page
 */
bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
  assert(page_id != INVALID_PAGE_ID);
  std::lock_guard<std::mutex> latch(latch_);
  
  Page *page = nullptr;
  bool ok = page_table_->Find(page_id, page); 
    
  if (ok && page->pin_count_ > 0) {
    if (is_dirty) {
      page->is_dirty_ = is_dirty;
    }
    if (--page->pin_count_ == 0) {
      replacer_->Insert(page);
    } 
    return true;
  } 
  return false;
}

/*
 * Used to flush a particular page of the buffer pool to disk. Should call the
 * write_page method of the disk manager
 * if page is not found in page table, return false
 * NOTE: make sure page_id != INVALID_PAGE_ID
 */
bool BufferPoolManager::FlushPage(page_id_t page_id) {
  assert(page_id != INVALID_PAGE_ID);
  std::lock_guard<std::mutex> latch(latch_);
  Page *page = nullptr;
  bool ok = page_table_->Find(page_id, page);

  if (ok) {
    disk_manager_->WritePage(page->page_id_, page->data_);
    return true;
  }
  return false;
}

/**
 * User should call this method for deleting a page. This routine will call
 * disk manager to deallocate the page. First, if page is found within page
 * table, buffer pool manager should be reponsible for removing this entry out
 * of page table, reseting page metadata and adding back to free list. Second,
 * call disk manager's DeallocatePage() method to delete from disk file. If
 * the page is found within page table, but pin_count != 0, return false
 */
bool BufferPoolManager::DeletePage(page_id_t page_id) {
  assert(page_id != INVALID_PAGE_ID);
  std::lock_guard<std::mutex> latch(latch_);
  Page *page = nullptr;
  bool ok = page_table_->Find(page_id, page);

  if (ok && page->pin_count_ == 0) {
    page->is_dirty_ = false; 
    page->page_id_ = INVALID_PAGE_ID;
    page_table_->Remove(page->page_id_); 
    free_list_->push_back(page);
    replacer_->Erase(page);
    disk_manager_->DeallocatePage(page_id);
    return true;
  } else if (!ok) {
    disk_manager_->DeallocatePage(page_id);
    return true;
  }

  return false;
}

/**
 * User should call this method if needs to create a new page. This routine
 * will call disk manager to allocate a page.
 * Buffer pool manager should be responsible to choose a victim page either
 * from free list or lru replacer(NOTE: always choose from free list first),
 * update new page's metadata, zero out memory and add corresponding entry
 * into page table. return nullptr if all the pages in pool are pinned
 */
Page *BufferPoolManager::NewPage(page_id_t &page_id) {
  std::lock_guard<std::mutex> latch(latch_);
  page_id = disk_manager_->AllocatePage();
  Page *page = nullptr;
  
  if (!free_list_->empty()) {
    // find from free list
    page = free_list_->front();
    free_list_->pop_front();
  } else {
    // find from lru replacer 
    bool ok = replacer_->Victim(page);
    if (!ok) {
      // all page are pinned 
      return nullptr;
    }

    assert(page_table_->Remove(page->page_id_)); 
    if (page->is_dirty_) {
      disk_manager_->WritePage(page->page_id_, page->data_);
    } 
  } 
  page->is_dirty_ = true; 
  page->pin_count_ = 1;
  page->page_id_ = page_id; 
  page_table_->Insert(page_id, page); 
  memset(page->data_, 0, PAGE_SIZE);
  return page;
}

  int BufferPoolManager::PinnedNum() const {
    int sum = 0;
    for (size_t i=0; i<pool_size_; i++) {
      if (pages_[i].pin_count_ > 0) {
	sum++;
      }
    }
    return sum;
  }
  
  std::vector<page_id_t> BufferPoolManager::PinnedPageId() const {
    std::vector<page_id_t> res;
    for (size_t i=0; i<pool_size_; i++) {
      if (pages_[i].pin_count_ > 0) {
        res.push_back(pages_[i].GetPageId());
      }
    }

    return res;
    // TODO(Handora): wht std::move wrong?
    // why
    // return std::move(res);
  }
} // namespace cmudb
