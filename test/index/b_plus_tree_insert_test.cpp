/**
 * b_plus_tree_simple_test.cpp
 */

#include "buffer/buffer_pool_manager.h"
#include "common/logger.h"
#include "index/b_plus_tree.h"
#include "vtable/virtual_table.h"
#include "gtest/gtest.h"

namespace cmudb {

  TEST(BPlusTreeInsertTests, SimpleTest1) {
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
    // create transaction
    Transaction *transaction = new Transaction(0);

    // create and fetch header_page
    page_id_t page_id;
    auto header_page = bpm->NewPage(page_id);
    (void)header_page;

    std::vector<int64_t> keys;
    
    for (int i=0; i<10000; i++) {
      keys.push_back(i);
    }

    for (auto key : keys) {
      int64_t value = key & 0xFFFFFFFF;
      rid.Set((int32_t)(key >> 32), value);
      index_key.SetFromInteger(key);
        if (key == 705) {

            std::cout << 1;
        }

      std::string prev = tree.ToString(true);

      tree.Insert(index_key, rid, transaction);
    }

    // std::cout << tree.ToString(true);

    EXPECT_EQ(1, bpm->PinnedNum());
    EXPECT_EQ(true, tree.CheckIntegrity());
    
    std::vector<RID> rids;
    for (auto key : keys) {
      rids.clear();
      index_key.SetFromInteger(key); 
      tree.GetValue(index_key, rids); 
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
