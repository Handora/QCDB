/*
 * b_plus_tree_delete_test.cpp
 */
#include <random>

#include "gtest/gtest.h"
#include "common/logger.h"
#include "index/b_plus_tree.h"
#include "vtable/virtual_table.h"
#include "page/b_plus_tree_internal_page.h"

namespace cmudb {

  TEST(BPlusTreeDeleteTests, SimpleTest) {
    Schema *key_schema = ParseCreateStatement("a bigint");
    GenericComparator<8> comparator(key_schema);

    DiskManager *disk_manager = new DiskManager("test.db");
    BufferPoolManager *bpm = new BufferPoolManager(500, disk_manager);

    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm, comparator);

    GenericKey<8> index_key;
    RID rid;
    Transaction *transaction = new Transaction(0);

    page_id_t page_id;
    auto header_page = bpm->NewPage(page_id);
    (void)header_page;

    std::vector<std::pair<int64_t, bool>> keys;
    for (int i=0; i<1000; i++) {
      std::default_random_engine generator;
      std::uniform_int_distribution<int> distribution(0,1);
      int dice_roll = distribution(generator); 
      keys.push_back({i, dice_roll});
    }

    // insert phase
    for (auto item: keys) {
      auto key = item.first; 
      int64_t value = key & 0xFFFFFFFF;
      rid.Set((int32_t)(key >> 32), value);
      index_key.SetFromInteger(key);
      tree.Insert(index_key, rid, transaction);
    }

    EXPECT_EQ(tree.CheckIntegrity(), true);
    EXPECT_EQ(1, bpm->PinnedNum());
    
    // delete phase
    for (auto item: keys) {
      auto key = item.first;
      auto deleted = item.second;
      index_key.SetFromInteger(key);
      if (deleted) {
  	std::cout << "Hello" << std::endl;
  	tree.Remove(index_key, transaction);
      }
    }

    EXPECT_EQ(tree.CheckIntegrity(), true);
    EXPECT_EQ(1, bpm->PinnedNum());

    // find phase
    std::vector<RID> rids;
    for (auto item: keys) {
      auto key = item.first;
      auto deleted = item.second;
      rids.clear();
      index_key.SetFromInteger(key);
      tree.GetValue(index_key, rids);
      if (deleted) {
  	EXPECT_EQ(0, rids.size());
  	continue;
      }
      
      EXPECT_EQ(rids.size(), 1);
      int64_t value = key & 0xFFFFFFFF; 
      EXPECT_EQ(rids[0].GetSlotNum(), value);
    }

    bpm->UnpinPage(HEADER_PAGE_ID, true);
    EXPECT_EQ(0, bpm->PinnedNum());
    delete transaction;
    delete disk_manager;
    delete bpm;
    remove("test.db");
    remove("test.log");
  }
  
  TEST(BPlusTreeDeleteTests, RandomDeleteTest) {
    Schema *key_schema = ParseCreateStatement("a bigint");
    GenericComparator<8> comparator(key_schema);

    DiskManager *disk_manager = new DiskManager("test.db");
    BufferPoolManager *bpm = new BufferPoolManager(500, disk_manager);

    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm, comparator);

    GenericKey<8> index_key;
    RID rid;
    Transaction *transaction = new Transaction(0);

    page_id_t page_id;
    auto header_page = bpm->NewPage(page_id);
    (void)header_page;

    std::vector<std::pair<int64_t, bool>> keys;
    for (int i=0; i<1000; i++) {
      srand((unsigned)time(0)+i);
      int d;
      d = (rand()%2); 
      keys.push_back({i, d});
    }

    // insert phase
    for (auto item: keys) {
      auto key = item.first; 
      int64_t value = key & 0xFFFFFFFF;
      rid.Set((int32_t)(key >> 32), value);
      index_key.SetFromInteger(key);
      tree.Insert(index_key, rid, transaction);
    }

    EXPECT_EQ(tree.CheckIntegrity(), true);
    EXPECT_EQ(1, bpm->PinnedNum()); 

    
    // delete phase
    for (auto item: keys) {
      auto key = item.first;
      auto deleted = item.second;
      index_key.SetFromInteger(key);
      // std::string prev = tree.ToString(true);
      if (deleted) {
	// std::cout << bpm->PinnedNum() << std::endl;
	tree.Remove(index_key, transaction);
	// std::cout << bpm->PinnedNum() << std::endl;
      }
      // if (!tree.CheckIntegrity()) {
      // 	std::cout << "The deleted one is: " << std::endl;
      // 	std::cout << key << std::endl;
      // 	std::cout << prev << std::endl;
      // 	std::cout << tree.ToString(true) << std::endl;
      // 	return ;
      // }
    }

    EXPECT_EQ(tree.CheckIntegrity(), true);
    EXPECT_EQ(1, bpm->PinnedNum());

    // find phase
    std::vector<RID> rids;
    for (auto item: keys) {
      auto key = item.first;
      auto deleted = item.second;
      rids.clear();
      index_key.SetFromInteger(key);
      tree.GetValue(index_key, rids);
      if (deleted) {
	EXPECT_EQ(0, rids.size());
	continue;
      }
      
      EXPECT_EQ(rids.size(), 1);
      int64_t value = key & 0xFFFFFFFF; 
      EXPECT_EQ(rids[0].GetSlotNum(), value);
    }

    bpm->UnpinPage(HEADER_PAGE_ID, true);
    EXPECT_EQ(0, bpm->PinnedNum());
    delete transaction;
    delete disk_manager;
    delete bpm;
    remove("test.db");
    remove("test.log");
  }
  
  TEST(BPlusTreeDeleteTests, BigRandomDeleteTest) {
    Schema *key_schema = ParseCreateStatement("a bigint");
    GenericComparator<8> comparator(key_schema);

    DiskManager *disk_manager = new DiskManager("test.db");
    BufferPoolManager *bpm = new BufferPoolManager(50, disk_manager);

    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm, comparator);

    GenericKey<8> index_key;
    RID rid;
    Transaction *transaction = new Transaction(0);

    page_id_t page_id;
    auto header_page = bpm->NewPage(page_id);
    (void)header_page;

    std::vector<std::pair<int64_t, bool>> keys;
    for (int i=0; i<10000; i++) {
      srand((unsigned)time(0)+i);
      int d;
      d = (rand()%2); 
      keys.push_back({i, d});
    }

    // insert phase
    for (auto item: keys) {
      auto key = item.first; 
      int64_t value = key & 0xFFFFFFFF;
      rid.Set((int32_t)(key >> 32), value);
      index_key.SetFromInteger(key);
      tree.Insert(index_key, rid, transaction);
    }

    EXPECT_EQ(tree.CheckIntegrity(), true);
    EXPECT_EQ(1, bpm->PinnedNum());
    
    // delete phase
    for (auto item: keys) {
      auto key = item.first;
      auto deleted = item.second;
      index_key.SetFromInteger(key);
      
      if (deleted) {
	// std::cout << key << std::endl;
	// std::string prev = tree.ToString(true);
	tree.Remove(index_key, transaction);

	// if (!tree.CheckIntegrity()) {
	//   std::cout << "The deleted one is: " << std::endl; 
	//   std::cout << key << std::endl;
	//   std::cout << prev << std::endl;
	//   std::cout << tree.ToString(true) << std::endl;
	//   return ;
	// }
      }
    }

    EXPECT_EQ(tree.CheckIntegrity(), true);
    EXPECT_EQ(1, bpm->PinnedNum()); 

    // find phase
    std::vector<RID> rids;
    for (auto item: keys) {
      auto key = item.first;
      auto deleted = item.second;
      rids.clear();
      index_key.SetFromInteger(key);
      tree.GetValue(index_key, rids);
      if (deleted) {
  	EXPECT_EQ(0, rids.size());
  	continue;
      }
      
      EXPECT_EQ(rids.size(), 1);
      int64_t value = key & 0xFFFFFFFF; 
      EXPECT_EQ(rids[0].GetSlotNum(), value);
    }

    bpm->UnpinPage(HEADER_PAGE_ID, true);
    EXPECT_EQ(0, bpm->PinnedNum());
    delete transaction;
    delete disk_manager;
    delete bpm;
    remove("test.db");
    remove("test.log");
  }

  TEST(BPlusTreeDeleteTests, SuperRandomDeleteTest) {
    Schema *key_schema = ParseCreateStatement("a bigint");
    GenericComparator<8> comparator(key_schema);

    DiskManager *disk_manager = new DiskManager("test.db");
    BufferPoolManager *bpm = new BufferPoolManager(50, disk_manager);

    BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm, comparator);

    GenericKey<8> index_key;
    RID rid;
    Transaction *transaction = new Transaction(0);

    page_id_t page_id;
    auto header_page = bpm->NewPage(page_id);
    (void)header_page;

    
    std::vector<int64_t> keys;
    std::map<int64_t, bool> key_set;

    
    for (int i=0; i<10000; i++) {
      srand(time(nullptr) + i);
      int64_t should_insert = rand() % 100000; 
      int should_delete = rand() % 2;
      auto res = key_set.insert({should_insert, should_delete});
      if (res.second) {
	keys.push_back(should_insert);
      }
    }

    for (auto key : keys) {
      int64_t value = key & 0xFFFFFFFF;
      rid.Set((int32_t)(key >> 32), value);
      index_key.SetFromInteger(key); 

      tree.Insert(index_key, rid, transaction);
    }
  
    EXPECT_EQ(tree.CheckIntegrity(), true);
    EXPECT_EQ(1, bpm->PinnedNum());
    
    // delete phase
    for (auto item: key_set) {
      auto key = item.first;
      auto deleted = item.second;
      index_key.SetFromInteger(key);
      
      if (deleted) {
	// std::cout << key << std::endl;
	// std::string prev = tree.ToString(true);
	tree.Remove(index_key, transaction);

	// if (!tree.CheckIntegrity()) {
	//   std::cout << "The deleted one is: " << std::endl; 
	//   std::cout << key << std::endl;
	//   std::cout << prev << std::endl;
	//   std::cout << tree.ToString(true) << std::endl;
	//   return ;
	// }
      }
    }

    EXPECT_EQ(tree.CheckIntegrity(), true);
    EXPECT_EQ(1, bpm->PinnedNum()); 

    // find phase
    std::vector<RID> rids;
    for (auto item: key_set) {
      auto key = item.first;
      auto deleted = item.second;
      rids.clear();
      index_key.SetFromInteger(key);
      tree.GetValue(index_key, rids);
      if (deleted) {
  	EXPECT_EQ(0, rids.size());
  	continue;
      }
      
      EXPECT_EQ(rids.size(), 1);
      int64_t value = key & 0xFFFFFFFF; 
      EXPECT_EQ(rids[0].GetSlotNum(), value);
    }

    bpm->UnpinPage(HEADER_PAGE_ID, true);
    EXPECT_EQ(0, bpm->PinnedNum());
    delete transaction;
    delete disk_manager;
    delete bpm;
    remove("test.db");
    remove("test.log");
  } 
}
