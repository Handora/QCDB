/**
 * lock_manager_test.cpp
 */

#include <thread>
#include <list>
#include <vector>
#include <algorithm>
#include "concurrency/transaction_manager.h"
#include "concurrency/lock_manager.h"
#include "gtest/gtest.h"

namespace cmudb {
/*
 * This test is only a sanity check. Please do not rely on this test
 * to check the correctness.
 */
  TEST(LockManagerTest, BasicTest) {
  
    LockManager lock_mgr{false}; 
    TransactionManager txn_mgr{&lock_mgr};
    RID rid{0, 0};
  
    std::thread t0([&] {
	Transaction txn(0) ; 
	bool res = lock_mgr.LockShared(&txn, rid); 
	EXPECT_EQ(res, true);
	EXPECT_EQ(txn.GetState(), TransactionState::GROWING);
	txn_mgr.Commit(&txn);
	EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED); 
      });

    std::thread t1([&] {      
	Transaction txn(1); 
	bool res = lock_mgr.LockShared(&txn, rid); 
	EXPECT_EQ(res, true);
	EXPECT_EQ(txn.GetState(), TransactionState::GROWING);
	txn_mgr.Commit(&txn);
	EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED); 
      });

    t0.join();
    t1.join();
  }

  TEST(LockManagerTest, ShareTest) {
  
    LockManager lock_mgr{false}; 
    TransactionManager txn_mgr{&lock_mgr};
    std::vector<int> order;
    // std::vector<std::thread> thread_vec;
    for (int i=0; i<1000; i++) {
      order.push_back(i);
    }
    
    std::thread t0([&order, &lock_mgr, &txn_mgr] {
	auto new_order = order;
	std::random_shuffle(new_order.begin(), new_order.end());
	std::vector<RID> rids;
	for (auto i: new_order) {
	  rids.push_back(RID(i, i));
	}

	Transaction txn(0) ;
	for (auto rid: rids) {
	  bool res = lock_mgr.LockShared(&txn, rid); 
	  EXPECT_EQ(res, true);
	}
	EXPECT_EQ(txn.GetState(), TransactionState::GROWING);
	txn_mgr.Commit(&txn);
	EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED);
	EXPECT_EQ(1000, txn.GetSharedLockSet()->size());
      });

    std::thread t1([&order, &lock_mgr, &txn_mgr] {
	auto new_order = order;
	std::random_shuffle(new_order.begin(), new_order.end());
	std::vector<RID> rids;
	for (auto i: new_order) {
	  rids.push_back(RID(i, i));
	}
      
	Transaction txn(1);
	for (auto rid: rids) {
	  bool res = lock_mgr.LockShared(&txn, rid); 
	  EXPECT_EQ(res, true);
	}

	EXPECT_EQ(txn.GetState(), TransactionState::GROWING);
	txn_mgr.Commit(&txn);
	EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED);
	EXPECT_EQ(1000, txn.GetSharedLockSet()->size());
      });

    t0.join();
    t1.join();
    if (!lock_mgr.IsClean()) {
      lock_mgr.ShowLock();
    }
    EXPECT_EQ(true, lock_mgr.IsClean());
  }

  TEST(LockManagerTest, ExclusiveTest) {
  
    LockManager lock_mgr{false}; 
    TransactionManager txn_mgr{&lock_mgr};
    std::vector<int> order;
    // std::vector<std::thread> thread_vec;
    for (int i=0; i<10; i++) {
      order.push_back(i);
    }
    
    std::thread t0([&] {
	auto new_order = order;
	std::random_shuffle(new_order.begin(), new_order.end());
	std::vector<RID> rids;
	for (auto i: new_order) {
	  rids.push_back(RID(i, i));
	}

	Transaction txn(0) ;

	// according to WAIT-DIE, txn0 should not be broken
	for (auto rid: rids) {
	  bool res = lock_mgr.LockExclusive(&txn, rid); 
	  EXPECT_EQ(res, true);
	} 
	EXPECT_EQ(txn.GetState(), TransactionState::GROWING);
	
	txn_mgr.Commit(&txn); 
	EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED);
	EXPECT_EQ(10, txn.GetExclusiveLockSet()->size()); 
      });

    std::thread t1([&] {
	auto new_order = order;
	std::random_shuffle(new_order.begin(), new_order.end());
	std::vector<RID> rids;
	for (auto i: new_order) {
	  rids.push_back(RID(i, i));
	}

      Again:
	Transaction txn(1); 
	for (auto rid: rids) {
	  bool res = lock_mgr.LockExclusive(&txn, rid); 
	  if (res == false) {
	    txn_mgr.Abort(&txn); 
	    goto Again;
	  }
	} 
	
	EXPECT_EQ(txn.GetState(), TransactionState::GROWING);
	txn_mgr.Commit(&txn);
	EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED);
	EXPECT_EQ(10, txn.GetExclusiveLockSet()->size());
      });

    t0.join();
    t1.join();
    EXPECT_EQ(true, lock_mgr.IsClean());
  }

  TEST(LockManagerTest, SuperExclusiveTest) {  
    LockManager lock_mgr{false}; 
    TransactionManager txn_mgr{&lock_mgr};
    std::vector<int> order;
    std::vector<std::thread> thread_vec;
    for (int i=0; i<1000; i++) {
      order.push_back(i);
    }

    for (int i=0; i<10; i++) {
      thread_vec.push_back(std::thread([i, &order, &txn_mgr, &lock_mgr] {
	    auto new_order = order;
	    std::random_shuffle(new_order.begin(), new_order.end());
	    std::vector<RID> rids;
	    for (auto x: new_order) {
	      rids.push_back(RID(x, x));
	    }

	  Again:
	    Transaction txn(i) ;

	    // according to WAIT-DIE, txn0 should not be broken
	    for (auto rid: rids) {
	      bool res = lock_mgr.LockExclusive(&txn, rid); 
              if (!res) {
		txn_mgr.Abort(&txn);
		goto Again;
	      }
	    } 
	    EXPECT_EQ(txn.GetState(), TransactionState::GROWING); 
	    txn_mgr.Commit(&txn); 
	    EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED);
	    EXPECT_EQ(1000, txn.GetExclusiveLockSet()->size()); 
	  }));
    }
    
    
    for (int i=0; i<10; i++) {
      thread_vec[i].join();
    }
    EXPECT_EQ(true, lock_mgr.IsClean());
  }

  TEST(LockManagerTest, UpgradeTest) {
  
    LockManager lock_mgr{false}; 
    TransactionManager txn_mgr{&lock_mgr};
    std::vector<int> order;
    // std::vector<std::thread> thread_vec;
    for (int i=0; i<1000; i++) {
      order.push_back(i);
    }
    
    std::thread t0([&] {
	auto new_order = order;
	std::random_shuffle(new_order.begin(), new_order.end());
	std::vector<RID> rids;
	for (auto i: new_order) {
	  rids.push_back(RID(i, i));
	}

	Transaction txn(0) ;

	// according to WAIT-DIE, txn0 should not be broken
	for (auto rid: rids) {
	  bool res = lock_mgr.LockShared(&txn, rid); 
	  EXPECT_EQ(res, true); 
	}
        
	for (auto rid: rids) {
	  bool res = lock_mgr.LockUpgrade(&txn, rid); 
	  EXPECT_EQ(res, true);
      } 
	
      EXPECT_EQ(txn.GetState(), TransactionState::GROWING); 
	txn_mgr.Commit(&txn); 
	EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED);
	EXPECT_EQ(0, txn.GetSharedLockSet()->size()); 
	EXPECT_EQ(1000, txn.GetExclusiveLockSet()->size()); 
      });

    std::thread t1([&] {
	auto new_order = order;
	std::random_shuffle(new_order.begin(), new_order.end());
	std::vector<RID> rids;
	for (auto i: new_order) {
	  rids.push_back(RID(i, i));
	}

      Again:
	Transaction txn(1); 
	for (auto rid: rids) {
	  bool res = lock_mgr.LockExclusive(&txn, rid); 
	  if (res == false) {
	    txn_mgr.Abort(&txn); 
	    goto Again;
	  }
	} 
	
	EXPECT_EQ(txn.GetState(), TransactionState::GROWING);
	txn_mgr.Commit(&txn);
	EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED); 
	EXPECT_EQ(0, txn.GetSharedLockSet()->size()); 
	EXPECT_EQ(1000, txn.GetExclusiveLockSet()->size()); 
      });

    t0.join();
    t1.join();
    EXPECT_EQ(true, lock_mgr.IsClean());
  }

  TEST(LockManagerTest, SuperUpgradeTest) {  
    LockManager lock_mgr{false}; 
    TransactionManager txn_mgr{&lock_mgr};
    std::vector<int> order;
    std::vector<std::thread> thread_vec;
    for (int i=0; i<10; i++) {
      order.push_back(i);
    }

    std::mutex mutex;
    for (int i=0; i<2; i++) {
      thread_vec.push_back(std::thread([i, &order, &txn_mgr, &lock_mgr, &mutex] () {
	    auto new_order = order;
	    std::random_shuffle(new_order.begin(), new_order.end());
	    std::vector<RID> rids;
	    for (auto x: new_order) {
	      rids.push_back(RID(x, x));
	    }

	  Again:
	    Transaction txn(i) ;

	    // according to WAIT-DIE, txn0 should not be broken
	    for (auto rid: rids) {
	      bool res = lock_mgr.LockShared(&txn, rid);
	      if (i == 0) {
		EXPECT_EQ(true, res);
	      } else {
		if (!res) {
		  txn_mgr.Abort(&txn);
		  usleep(300);
		  goto Again;
		}
	      }
	    }
	    mutex.lock(); 
	    mutex.unlock();
	    for (auto rid: rids) {
	      bool res = lock_mgr.LockUpgrade(&txn, rid);
	      if (i==0) {
		EXPECT_EQ(true, res);
	      }
	      usleep(300);
	      if (!res) {
		txn_mgr.Abort(&txn);
		goto Again;
	      }
	    }
	    mutex.lock(); 
	    mutex.unlock();

	    EXPECT_EQ(txn.GetState(), TransactionState::GROWING); 
	    txn_mgr.Commit(&txn);
	    mutex.lock(); 
	    mutex.unlock();
	    EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED);
	    EXPECT_EQ(10, txn.GetExclusiveLockSet()->size()); 
	  }));
    }
    
    
    for (int i=0; i<2; i++) {
      thread_vec[i].join();
    }
    EXPECT_EQ(true, lock_mgr.IsClean());
  }

  TEST(LockManagerTest, SuperSuperUpdateTest) {  
    LockManager lock_mgr{false}; 
    TransactionManager txn_mgr{&lock_mgr};
    std::vector<int> order;
    std::vector<std::thread> thread_vec;
    for (int i=0; i<1000; i++) {
      order.push_back(i);
    }

    std::mutex mutex;
    for (int i=0; i<10; i++) {
      thread_vec.push_back(std::thread([i, &order, &txn_mgr, &lock_mgr, &mutex] () {
	    auto new_order = order;
	    std::random_shuffle(new_order.begin(), new_order.end());
	    std::vector<RID> rids;
	    for (auto x: new_order) {
	      rids.push_back(RID(x, x));
	    }

	  Again:
	    Transaction txn(i) ;

	    // according to WAIT-DIE, txn0 should not be broken
	    for (auto rid: rids) {
	      bool res = lock_mgr.LockShared(&txn, rid);
	      if (i == 0) {
		EXPECT_EQ(true, res);
	      } else {
		if (!res) {
		  txn_mgr.Abort(&txn);
		  usleep(300);
		  goto Again;
		}
	      }
	    }
	    mutex.lock(); 
	    mutex.unlock();
	    for (auto rid: rids) {
	      bool res = lock_mgr.LockUpgrade(&txn, rid);
	      if (i==0) {
		EXPECT_EQ(true, res);
	      }
	      usleep(300);
	      if (!res) {
		txn_mgr.Abort(&txn);
		goto Again;
	      }
	    }
	    mutex.lock(); 
	    mutex.unlock();

	    EXPECT_EQ(txn.GetState(), TransactionState::GROWING); 
	    txn_mgr.Commit(&txn);
	    mutex.lock(); 
	    mutex.unlock();
	    EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED);
	    EXPECT_EQ(1000, txn.GetExclusiveLockSet()->size()); 
	  }));
    }
    
    
    for (int i=0; i<10; i++) {
      thread_vec[i].join();
    }
    EXPECT_EQ(true, lock_mgr.IsClean());
  }

  TEST(LockManagerTest, RandomTest) {
    LockManager lock_mgr{false}; 
    TransactionManager txn_mgr{&lock_mgr}; 
    std::vector<std::thread> thread_vec; 

    std::mutex mutex;
    for (int i=0; i<10; i++) {
      thread_vec.push_back(std::thread([i, &txn_mgr, &lock_mgr, &mutex] () {
	    srand(time(nullptr)+i);
	    std::vector<int> new_order;
	    for (int i=0; i<1000; i++) {
	      new_order.push_back(random() % 10);
	    }

	    std::vector<RID> rids;
	    for (auto x: new_order) {
	      rids.push_back(RID(x, x));
	    }

	  Again:
	    Transaction txn(i) ;

	    // according to WAIT-DIE, txn0 should not be broken
	    for (auto rid: rids) {
	      int how = rand() % 2;
	      bool res;
	      if (how == 0) {
		if (txn.GetSharedLockSet()->count(rid)) {
		  res = true;
		} else if (txn.GetExclusiveLockSet()->count(rid)) {
		  res = true;
		} else {
		  res = lock_mgr.LockShared(&txn, rid);
		}
	      } else if (how == 1) {
		if (txn.GetSharedLockSet()->count(rid)) {
		  res = lock_mgr.LockUpgrade(&txn, rid);
		} else if (txn.GetExclusiveLockSet()->count(rid)) {
		  res = true;
		} else {
		  res = lock_mgr.LockExclusive(&txn, rid);
		}
	      }
	      
	      if (i == 0) {
		EXPECT_EQ(true, res);
	      } else {
		if (!res) {
		  txn_mgr.Abort(&txn);
		  usleep(300);
		  goto Again;
		}
	      }
	    }

	    EXPECT_EQ(txn.GetState(), TransactionState::GROWING); 
	    txn_mgr.Commit(&txn);
	    mutex.lock(); 
	    mutex.unlock();
	    EXPECT_EQ(txn.GetState(), TransactionState::COMMITTED); 
	  }));
    }
    
    
    for (int i=0; i<10; i++) {
      thread_vec[i].join();
    }
    EXPECT_EQ(true, lock_mgr.IsClean());
  }

} // namespace cmudb
