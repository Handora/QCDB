/**
 * b_plus_tree_test.cpp
 */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <thread>

#include "buffer/buffer_pool_manager.h"
#include "common/logger.h"
#include "index/b_plus_tree.h"
#include "vtable/virtual_table.h"
#include "gtest/gtest.h"

namespace cmudb {
// helper function to launch multiple threads

  std::mutex mu;
  
  template <typename... Args>
  void LaunchParallelTest(uint64_t num_threads, Args &&... args) {
    std::vector<std::thread> thread_group;

    // Launch a group of threads
    for (uint64_t thread_itr = 0; thread_itr < num_threads; ++thread_itr) {
      thread_group.push_back(std::thread(args..., thread_itr));
    }

    // Join the threads with the main thread
    for (uint64_t thread_itr = 0; thread_itr < num_threads; ++thread_itr) {
      thread_group[thread_itr].join();
    }
  }

// helper function to insert
  void InsertHelper(BPlusTree<GenericKey<8>, RID, GenericComparator<8>> &tree,
		    const std::vector<int64_t> &keys,
		    __attribute__((unused)) uint64_t thread_itr = 0) {
    GenericKey<8> index_key;
    RID rid;
    // create transaction
    Transaction *transaction = new Transaction(0);
    for (auto key : keys) {
      int64_t value = key & 0xFFFFFFFF;
      rid.Set((int32_t)(key >> 32), value);
      index_key.SetFromInteger(key);
      tree.Insert(index_key, rid, transaction);
    }
    delete transaction;
  }

// helper function to seperate insert
  void InsertHelperSplit(
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> &tree,
    const std::vector<int64_t> &keys, int total_threads,
    __attribute__((unused)) uint64_t thread_itr) {
    GenericKey<8> index_key;
    RID rid;
    // create transaction
    Transaction *transaction = new Transaction(0);
    for (auto key : keys) {
      if ((uint64_t)key % total_threads == thread_itr) {
	int64_t value = key & 0xFFFFFFFF;
	rid.Set((int32_t)(key >> 32), value);
	index_key.SetFromInteger(key);
	auto prev = tree.ToString(true);
	tree.Insert(index_key, rid, transaction);
	if (!tree.CheckIntegrity()) {
	  mu.lock();
	  std::cout << key << std::endl;
	  std::cout << prev << std::endl;
	  std::cout << tree.ToString(true);
	  flush(std::cout);
	  mu.unlock();
	  exit(1);
	}
      }
    }
    delete transaction;
  }

// helper function to delete
  void DeleteHelper(BPlusTree<GenericKey<8>, RID, GenericComparator<8>> &tree,
		    const std::vector<int64_t> &remove_keys,
		    __attribute__((unused)) uint64_t thread_itr = 0) {
    GenericKey<8> index_key;
    // create transaction
    Transaction *transaction = new Transaction(0);
    for (auto key : remove_keys) {
      index_key.SetFromInteger(key);
      tree.Remove(index_key, transaction);
    }
    delete transaction;
  }

// helper function to seperate delete
  void DeleteHelperSplit(
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> &tree,
    const std::vector<int64_t> &remove_keys, int total_threads,
    __attribute__((unused)) uint64_t thread_itr) {
    GenericKey<8> index_key;
    // create transaction
    Transaction *transaction = new Transaction(0);
    for (auto key : remove_keys) {
      if ((uint64_t)key % total_threads == thread_itr) {
	index_key.SetFromInteger(key);
	tree.Remove(index_key, transaction);
      }
    }
    delete transaction;
  }

  TEST(BPlusTreeConcurrentTest, InsertTest1) {
    // create KeyComparator and index schema
    Schema *key_schema = ParseCreateStatement("a bigint");
    GenericComparator<8> comparator(key_schema);

    DiskManager *disk_manager = new DiskManager("test.db");
    BufferPoolManager *bpm = new BufferPoolManager(50, disk_manager);
    // create b+ tree
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm,
							     comparator);
    // create and fetch header_page
    page_id_t page_id;
    auto header_page = bpm->NewPage(page_id);
    (void)header_page;
    // keys to Insert
    std::vector<int64_t> keys;
    int64_t scale_factor = 100;
    for (int64_t key = 1; key < scale_factor; key++) {
      keys.push_back(key);
    }
    LaunchParallelTest(2, InsertHelper, std::ref(tree), keys);

    std::vector<RID> rids;
    GenericKey<8> index_key;
    for (auto key : keys) {
      rids.clear();
      index_key.SetFromInteger(key);
      tree.GetValue(index_key, rids);
      EXPECT_EQ(rids.size(), 1);

      int64_t value = key & 0xFFFFFFFF;
      EXPECT_EQ(rids[0].GetSlotNum(), value);
    }

    int64_t start_key = 1;
    int64_t current_key = start_key;
    index_key.SetFromInteger(start_key);
    for (auto iterator = tree.Begin(index_key); iterator.isEnd() == false;
	 ++iterator) {
      auto location = (*iterator).second;
      EXPECT_EQ(location.GetPageId(), 0);
      EXPECT_EQ(location.GetSlotNum(), current_key);
      current_key = current_key + 1;
    }

    EXPECT_EQ(current_key, keys.size() + 1);

    bpm->UnpinPage(HEADER_PAGE_ID, true);

    delete disk_manager;
    delete bpm;
    remove("test.db");
    remove("test.log");
  }

  TEST(BPlusTreeConcurrentTest, InsertTest2) {
    // create KeyComparator and index schema
    Schema *key_schema = ParseCreateStatement("a bigint");
    GenericComparator<8> comparator(key_schema);
    DiskManager *disk_manager = new DiskManager("test.db");
    BufferPoolManager *bpm = new BufferPoolManager(50, disk_manager);
    // create b+ tree
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm,
							     comparator);
    // create and fetch header_page
    page_id_t page_id;
    auto header_page = bpm->NewPage(page_id);
    (void)header_page;
    // keys to Insert
    std::vector<int64_t> keys;
    int64_t scale_factor = 100;
    for (int64_t key = 1; key < scale_factor; key++) {
      keys.push_back(key);
    }
    LaunchParallelTest(2, InsertHelperSplit, std::ref(tree), keys, 2);

    std::vector<RID> rids;
    GenericKey<8> index_key;
    for (auto key : keys) {
      rids.clear();
      index_key.SetFromInteger(key);
      tree.GetValue(index_key, rids);
      EXPECT_EQ(rids.size(), 1);

      int64_t value = key & 0xFFFFFFFF;
      EXPECT_EQ(rids[0].GetSlotNum(), value);
    }

    int64_t start_key = 1;
    int64_t current_key = start_key;
    index_key.SetFromInteger(start_key);
    for (auto iterator = tree.Begin(index_key); iterator.isEnd() == false;
	 ++iterator) {
      auto location = (*iterator).second;
      EXPECT_EQ(location.GetPageId(), 0);
      EXPECT_EQ(location.GetSlotNum(), current_key);
      current_key = current_key + 1;
    }

    EXPECT_EQ(current_key, keys.size() + 1);

    bpm->UnpinPage(HEADER_PAGE_ID, true);

    delete disk_manager;
    delete bpm;
    remove("test.db");
    remove("test.log");
  }

  TEST(BPlusTreeConcurrentTest, DeleteTest1) {
    // create KeyComparator and index schema
    Schema *key_schema = ParseCreateStatement("a bigint");
    GenericComparator<8> comparator(key_schema);

    DiskManager *disk_manager = new DiskManager("test.db");
    BufferPoolManager *bpm = new BufferPoolManager(50, disk_manager);
    // create b+ tree
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm,
							     comparator);
    GenericKey<8> index_key;
    RID rid;
    // create and fetch header_page
    page_id_t page_id;
    auto header_page = bpm->NewPage(page_id);
    (void)header_page;
    // sequential insert
    std::vector<int64_t> keys = {1, 2, 3, 4, 5};
    InsertHelper(tree, keys);

    std::vector<int64_t> remove_keys = {1, 5, 3, 4};
    LaunchParallelTest(2, DeleteHelper, std::ref(tree), remove_keys);

    int64_t start_key = 2;
    int64_t current_key = start_key;
    int64_t size = 0;
    index_key.SetFromInteger(start_key);
    for (auto iterator = tree.Begin(index_key); iterator.isEnd() == false;
	 ++iterator) {
      auto location = (*iterator).second;
      EXPECT_EQ(location.GetPageId(), 0);
      EXPECT_EQ(location.GetSlotNum(), current_key);
      current_key = current_key + 1;
      size = size + 1;
    }

    EXPECT_EQ(size, 1);

    bpm->UnpinPage(HEADER_PAGE_ID, true);

    delete disk_manager;
    delete bpm;
    remove("test.db");
    remove("test.log");
  }

  TEST(BPlusTreeConcurrentTest, DeleteTest2) {
    // create KeyComparator and index schema
    Schema *key_schema = ParseCreateStatement("a bigint");
    GenericComparator<8> comparator(key_schema);

    DiskManager *disk_manager = new DiskManager("test.db");
    BufferPoolManager *bpm = new BufferPoolManager(50, disk_manager);
    // create b+ tree
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm,
							     comparator);
    GenericKey<8> index_key;
    RID rid;
    // create and fetch header_page
    page_id_t page_id;
    auto header_page = bpm->NewPage(page_id);
    (void)header_page;

    // sequential insert
    std::vector<int64_t> keys = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    InsertHelper(tree, keys);

    std::vector<int64_t> remove_keys = {1, 4, 3, 2, 5, 6};
    LaunchParallelTest(2, DeleteHelperSplit, std::ref(tree), remove_keys, 2);

    int64_t start_key = 7;
    int64_t current_key = start_key;
    int64_t size = 0;
    index_key.SetFromInteger(start_key);
    for (auto iterator = tree.Begin(index_key); iterator.isEnd() == false;
	 ++iterator) {
      auto location = (*iterator).second;
      EXPECT_EQ(location.GetPageId(), 0);
      EXPECT_EQ(location.GetSlotNum(), current_key);
      current_key = current_key + 1;
      size = size + 1;
    }

    EXPECT_EQ(size, 4);

    bpm->UnpinPage(HEADER_PAGE_ID, true);
    delete disk_manager;
    delete bpm;
    remove("test.db");
    remove("test.log");
  }

  TEST(BPlusTreeConcurrentTest, MixTest) {
    // create KeyComparator and index schema
    Schema *key_schema = ParseCreateStatement("a bigint");
    GenericComparator<8> comparator(key_schema);

    DiskManager *disk_manager = new DiskManager("test.db");
    BufferPoolManager *bpm = new BufferPoolManager(50, disk_manager);
    // create b+ tree
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm,
							     comparator);
    GenericKey<8> index_key;
    RID rid;

    // create and fetch header_page
    page_id_t page_id;
    auto header_page = bpm->NewPage(page_id);
    (void)header_page;
    // first, populate index
    std::vector<int64_t> keys = {1, 2, 3, 4, 5};
    InsertHelper(tree, keys);

    // concurrent insert
    keys.clear();
    for (int i = 6; i <= 10; i++)
      keys.push_back(i);
    LaunchParallelTest(1, InsertHelper, std::ref(tree), keys);
    // concurrent delete
    std::vector<int64_t> remove_keys = {1, 4, 3, 5, 6};
    LaunchParallelTest(1, DeleteHelper, std::ref(tree), remove_keys);

    int64_t start_key = 2;
    int64_t size = 0;
    index_key.SetFromInteger(start_key);
    for (auto iterator = tree.Begin(index_key); iterator.isEnd() == false;
	 ++iterator) {
      size = size + 1;
    }

    EXPECT_EQ(size, 5);

    bpm->UnpinPage(HEADER_PAGE_ID, true);
    delete disk_manager;
    delete bpm;
    remove("test.db");
    remove("test.log");
  }

  TEST(BPlusTreeConcurrentTest, SuperMixTest) {
    // create KeyComparator and index schema
    Schema *key_schema = ParseCreateStatement("a bigint");
    GenericComparator<8> comparator(key_schema);

    DiskManager *disk_manager = new DiskManager("test.db");
    BufferPoolManager *bpm = new BufferPoolManager(50, disk_manager);
    // create b+ tree
    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm,
							     comparator); 
    RID rid;

    // create and fetch header_page
    page_id_t page_id;
    auto header_page = bpm->NewPage(page_id);
    (void)header_page;
    // first, populate index
    std::vector<int64_t> keys;
    for (int i=0; i<10000; i++) {
      keys.push_back(i);
    }
    // std::random_shuffle(keys.begin(), keys.end());
    
    InsertHelper(tree, std::vector<int64_t>(keys.begin(), keys.begin()+3000));
    EXPECT_EQ(true, tree.CheckIntegrity());
    EXPECT_EQ(1, bpm->PinnedNum());
    // concurrent insert
    LaunchParallelTest(10, InsertHelperSplit, std::ref(tree), std::vector<int64_t>(keys.begin()+3000, keys.end()), 10); 
    EXPECT_EQ(true, tree.CheckIntegrity());
    EXPECT_EQ(1, bpm->PinnedNum());
    // concurrent delete 
    LaunchParallelTest(10, DeleteHelperSplit, std::ref(tree), keys, 10);
    EXPECT_EQ(true, tree.CheckIntegrity());
    EXPECT_EQ(1, bpm->PinnedNum());

    bpm->UnpinPage(HEADER_PAGE_ID, true);
    EXPECT_EQ(0, bpm->PinnedNum());
    
    delete disk_manager;
    delete bpm;
    remove("test.db");
    remove("test.log");
  }

} // namespace cmudb
