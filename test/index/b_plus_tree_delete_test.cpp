/*
 * b_plus_tree_delete_test.cpp
 */
#include "gtest/gtest.h"
#include "common/logger.h"
#include "index/b_plus_tree.h"
#include "vtable/virtual_table.h"

namespace cmudb {

  TEST(BPlusTreeTests, SimpleTest) {
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

    std::vector<int64_t> keys = {1, 2, 3, 4, 5};
    for (auto key: keys) {
      int64_t value = key & 0xFFFFFFFF;
      rid.Set((int32_t)(key >> 32), value);
      index_key.SetFromInteger(key);
      tree.Insert(index_key, rid, transaction);
    }

    std::vector<RID> rids;
    for (auto key: keys) {
      rids.clear();
      index_key.SetFromInteger(key);
      tree.GetValue(index_key, rids);
      EXPECT_EQ(rids.size(), 1);

      int64_t value = key & 0xFFFFFFFF;
      EXPECT_EQ(rids[0].GetSlotNum(), value);
    }

    index_key.SetFromInteger(1);
    tree.Remove(index_key);
    rids.clear();
    tree.GetValue(index_key, rids); 
    EXPECT_EQ(0, rids.size()); 
  } 
}
