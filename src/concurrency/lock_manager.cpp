/**
 * lock_manager.cpp
 */

#include "concurrency/lock_manager.h"
#include <cassert>
#include <algorithm>

namespace cmudb {

  bool LockManager::LockShared(Transaction *txn, const RID &rid) {
    // RAII latch protect the lock table for concurrent visits
    std::unique_lock<std::mutex> latch(lock_table_latch_);
    assert(txn->GetState() != TransactionState::SHRINKING);

    if (txn->GetState() == TransactionState::ABORTED) {
      return false;
    }

    auto res_itr = lock_table_.find(rid);
    if (res_itr == lock_table_.end() 
	|| res_itr->second.lock_list_.size() == 0) {
      // No lock is held, so we can require the lock
      if (res_itr == lock_table_.end()) {
	lock_table_.insert({rid, LockList()});
      }
      
      res_itr->second.lock_list_.push_back({txn, true, LockType::SHARED, std::promise<bool>()}); 
      txn->GetSharedLockSet()->insert(rid);
      return true;
    } else {
      // if there are some txns waiting while the state is SHARED
      if (res_itr->second.lock_list_.front().lock_type_ == LockType::SHARED) {
	// while some other one is also waiting for the lock
	if (!res_itr->second.lock_list_.back().grated_) {
	  bool ok = CheckForWaitDie(txn, rid);
	  if (ok) {
	    res_itr->second.lock_list_.push_back({txn, false, LockType::SHARED, std::promise<bool>()});
	  } else {
	    txn->SetState(TransactionState::ABORTED);
	    return false;
	  }
	} else {
	  res_itr->second.lock_list_.push_back({txn, true, LockType::SHARED});
	  txn->GetSharedLockSet()->insert(rid);
	  return true;
	}
      } else {
	if (res_itr->second.lock_list_.size() == 1 && res_itr->second.lock_list_.front().txn_ == txn) {
	  txn->GetSharedLockSet()->insert(rid);
	  return true;
	}
	
	bool ok = CheckForWaitDie(txn, rid);
	if (ok) {
	  res_itr->second.lock_list_.push_back({txn, false, LockType::SHARED});
	} else {
	  return false;
	}
      }
    }

    condition_.wait(latch);
    if (txn->GetState() == TransactionState::ABORTED) {
      return false;
    }

    txn->GetSharedLockSet()->insert(rid);
    return true;
  }

  bool LockManager::LockExclusive(Transaction *txn, const RID &rid) {
    // RAII latch protect the lock table for concurrent visits
    std::unique_lock<std::mutex> latch(lock_table_latch_);
    assert(txn->GetState() != TransactionState::SHRINKING);

    if (txn->GetState() == TransactionState::ABORTED) {
      return false;
    }

    auto res_itr = lock_table_.find(rid);
    if (res_itr == lock_table_.end() 
	|| res_itr->second.lock_list_.size() == 0) {
      // No lock is held, so we can require the lock
      if (res_itr == lock_table_.end()) {
	lock_table_.insert({rid, LockList()});
      }

      res_itr->second.lock_list_.push_back({txn, true, LockType::EXCLUSIVE}); 

      txn->GetExclusiveLockSet()->insert(rid);
      return true;
    } else {

      // check for WAIT-DIE
      if (CheckForWaitDie(txn, rid)) {
	// wait for notifying
	res_itr->second.lock_list_.push_back({txn, false, LockType::EXCLUSIVE}); 
      } else {
	// break the WAIT_DIE fule
	return false;
      }

      // wait until being notified
      condition_.wait(latch);
      if (txn->GetState() == TransactionState::ABORTED) {
	return false;
      }

      txn->GetExclusiveLockSet()->insert(rid);
      return true;
    }
  }

  bool LockManager::LockUpgrade(Transaction *txn, const RID &rid) {
    std::unique_lock<std::mutex> latch(lock_table_latch_);
    assert(txn->GetState() != TransactionState::SHRINKING);
    
    auto res_itr = lock_table_.find(rid);
    assert(res_itr != lock_table_.end());
    int all_shared = 0;
    auto txn_info_itr = std::find_if(res_itr->second.lock_list_.begin(),
				     res_itr->second.lock_list_.end(),
				     [&](const TransactionInfo &elem) {
				       return elem.txn_ == txn;
				     });

    // for removing the warning
    if (txn_info_itr != res_itr->second.lock_list_.end())
      assert(txn_info_itr != res_itr->second.lock_list_.end());
    assert(txn_info_itr->granted_ && txn_info_itr->lock_type_ == LockType::SHARED);

    std::for_each(res_itr->second.lock_list_.begin(),
		  res_itr->second.lock_list_.end(),
		  [&](const TransactionInfo &elem) {
		    if (elem.grated_)
		      all_shared++;
		  });

    if (all_shared != 1) {
      if (CheckForWaitDie(txn, rid)) {
	res_itr->second.lock_list_.push_back({txn, false, LockType::UPDATE});
	condition_.wait(latch);
      } else
	return false;
    }

    txn->GetExclusiveLockSet()->insert(rid);
    return true;
  }

  bool LockManager::Unlock(Transaction *txn, const RID &rid) {
    std::unique_lock<std::mutex> latch(lock_table_latch_);

    // strict 2pl should be aithor committed or aborted
    assert(strict_2PL_ && txn->GetState() != TransactionState::COMMITTED && txn->GetState != TransactionState::ABORTED);

    if (!strict_2PL_ && txn->GetState() == TransactionState::GROWING) {
      txn->SetState(TransactionState::SHRINKING); 
    }

    auto res_itr = lock_table_.find(rid);
    assert(res_itr != lock_table_.end());

    auto res_list = res_itr->second.lock_list_;
    auto txn_info_itr = std::find_if(res_list.begin(),
				     res_list.end(),
		 [&](const TransactionInfo& elem) {
                   return elem.grated_ == true && elem.txn_ == txn;
		 });

    assert(txn_info_itr != res_list.end());
    if (txn_info_itr->lock_type_ == LockType::EXCLUSIVE) {
      // get the next one for doing if not null
      auto next_txn_info_itr = txn_info_itr++;
      if (next_txn_info_itr != res_list.end()) {
	txn_info_itr->grated_ = true;
	// how to notify special thread
      } 
    } else if (txn_info_itr->lock_type_ == LockType::SHARED) {
      int shared_num = 0; 
      std::for_each(res_list.begin(),
		    res_list.end(),
		    [&](const TransactionInfo &elem) {
		      if (elem.grated_)
			shared_num++; 
		    });
      if (shared_num == 1) {
	
      }
    }

    res_list.erase(txn_info_itr);
    return true;
  }

  bool LockManager::CheckForWaitDie(Transaction *txn, const RID &rid) {
    for (const auto &ele: lock_table_[rid].lock_list_) {
      if (ele.txn_->GetTransactionId() < txn->GetTransactionId()) {
	return false;
      }
    }
    return true;
  }
} // namespace cmudb
