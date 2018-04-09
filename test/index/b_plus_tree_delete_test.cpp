/*
 * b_plus_tree_delete_test.cpp
 */
#include <random>

#include "gtest/gtest.h"
#include "common/logger.h"
#include "index/b_plus_tree.h"
#include "vtable/virtual_table.h"

namespace cmudb {

  // TEST(BPlusTreeDeleteTests, SimpleTest) {
  //   Schema *key_schema = ParseCreateStatement("a bigint");
  //   GenericComparator<8> comparator(key_schema);

  //   DiskManager *disk_manager = new DiskManager("test.db");
  //   BufferPoolManager *bpm = new BufferPoolManager(500, disk_manager);

  //   BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm, comparator);

  //   GenericKey<8> index_key;
  //   RID rid;
  //   Transaction *transaction = new Transaction(0);

  //   page_id_t page_id;
  //   auto header_page = bpm->NewPage(page_id);
  //   (void)header_page;

  //   std::vector<std::pair<int64_t, bool>> keys;
  //   for (int i=0; i<1000; i++) {
  //     std::default_random_engine generator;
  //     std::uniform_int_distribution<int> distribution(0,1);
  //     int dice_roll = distribution(generator); 
  //     keys.push_back({i, dice_roll});
  //   }

  //   // insert phase
  //   for (auto item: keys) {
  //     auto key = item.first; 
  //     int64_t value = key & 0xFFFFFFFF;
  //     rid.Set((int32_t)(key >> 32), value);
  //     index_key.SetFromInteger(key);
  //     tree.Insert(index_key, rid, transaction);
  //   }

  //   EXPECT_EQ(tree.CheckIntegrity(), true);
  //   EXPECT_EQ(1, bpm->PinnedNum());
    
  //   // delete phase
  //   for (auto item: keys) {
  //     auto key = item.first;
  //     auto deleted = item.second;
  //     index_key.SetFromInteger(key);
  //     if (deleted) {
  // 	std::cout << "Hello" << std::endl;
  // 	tree.Remove(index_key, transaction);
  //     }
  //   }

  //   EXPECT_EQ(tree.CheckIntegrity(), true);
  //   EXPECT_EQ(1, bpm->PinnedNum());

  //   // find phase
  //   std::vector<RID> rids;
  //   for (auto item: keys) {
  //     auto key = item.first;
  //     auto deleted = item.second;
  //     rids.clear();
  //     index_key.SetFromInteger(key);
  //     tree.GetValue(index_key, rids);
  //     if (deleted) {
  // 	EXPECT_EQ(0, rids.size());
  // 	continue;
  //     }
      
  //     EXPECT_EQ(rids.size(), 1);
  //     int64_t value = key & 0xFFFFFFFF; 
  //     EXPECT_EQ(rids[0].GetSlotNum(), value);
  //   }
  // }
  
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
      if (deleted) {
	tree.Remove(index_key, transaction); 
      }
      if (bpm->PinnedNum() != 1) {
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
  }
  
  // TEST(BPlusTreeDeleteTests, BigRandomDeleteTest) {
  //   Schema *key_schema = ParseCreateStatement("a bigint");
  //   GenericComparator<8> comparator(key_schema);

  //   DiskManager *disk_manager = new DiskManager("test.db");
  //   BufferPoolManager *bpm = new BufferPoolManager(5000, disk_manager);

  //   BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", bpm, comparator);

  //   GenericKey<8> index_key;
  //   RID rid;
  //   Transaction *transaction = new Transaction(0);

  //   page_id_t page_id;
  //   auto header_page = bpm->NewPage(page_id);
  //   (void)header_page;

  //   std::vector<std::pair<int64_t, bool>> keys;
  //   for (int i=0; i<10000; i++) {
  //     std::default_random_engine generator;
  //     std::uniform_int_distribution<int> distribution(0,1);
  //     int dice_roll = distribution(generator); 
  //     keys.push_back({i, dice_roll});
  //   }

  //   // insert phase
  //   for (auto item: keys) {
  //     auto key = item.first; 
  //     int64_t value = key & 0xFFFFFFFF;
  //     rid.Set((int32_t)(key >> 32), value);
  //     index_key.SetFromInteger(key);
  //     tree.Insert(index_key, rid, transaction);
  //   }

  //   EXPECT_EQ(tree.CheckIntegrity(), true);
  //   EXPECT_EQ(1, bpm->PinnedNum());
    
  //   // delete phase
  //   for (auto item: keys) {
  //     auto key = item.first;
  //     auto deleted = item.second;
  //     index_key.SetFromInteger(key);
  //     if (deleted) {
  // 	tree.Remove(index_key, transaction);
  //     }
  //   }

  //   EXPECT_EQ(tree.CheckIntegrity(), true);
  //   EXPECT_EQ(1, bpm->PinnedNum());

  //   // find phase
  //   std::vector<RID> rids;
  //   for (auto item: keys) {
  //     auto key = item.first;
  //     auto deleted = item.second;
  //     rids.clear();
  //     index_key.SetFromInteger(key);
  //     tree.GetValue(index_key, rids);
  //     if (deleted) {
  // 	EXPECT_EQ(0, rids.size());
  // 	continue;
  //     }
      
  //     EXPECT_EQ(rids.size(), 1);
  //     int64_t value = key & 0xFFFFFFFF; 
  //     EXPECT_EQ(rids[0].GetSlotNum(), value);
  //   }
  // } 
}
