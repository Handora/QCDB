/**
 * lock_manager.cpp
 */

#include "concurrency/lock_manager.h"

namespace cmudb {

  bool LockManager::LockShared(Transaction *txn, const RID &rid) {
    if (txn->GetState() == TransactionState::ABORTED) {
      return false;
    }

    // RAII latch protect the lock table for concurrent visits
    std::unique_lock<std::mutex> latch(lock_table_latch_);

    auto res_itr = lock_table_.find(rid);
    if (res_itr == lock_table_.end() 
	|| res_itr->second.lock_list_.size() == 0) {
      // No lock is held, so we can require the lock
      if (res_itr == lock_table_.end()) {
	lock_table_.insert({rid, LockList()});
      }

      lock_table_[rid].lock_list_.push_back({txn, true, LockType::SHARED});
      lock_table_[rid].lock_type_ = LockType::SHARED;

      return true;
    } else {
      // if there are some txns waiting while the state is SHARED
      if (lock_table_[rid].lock_type_ == LockType::SHARED) {
	lock_table_[rid].lock_list_.push_back({txn, true, LockType::SHARED});
	return true;
      } else {
	// TODO(handora): There is a situation that read to a just txn
	// write only, we should grant for this
	for (auto ele: lock_table_[rid].lock_list_) {
	  if (!ele.grated_) {
	    continue;
	  }
	  if (txn->GetTransactionId() > ele.txn_->GetTransactionId()) {
	    // not satisfied with *WAIT_DIE*
	    return false;
	  }
	}

	lock_table_[rid].lock_list_.push_back({txn, false, LockType::SHARED});
	condition_.wait(latch);

	// TODO(handora): should there be a check for sth
	lock_table_[rid].lock_list_.push_back({txn, true, LockType::SHARED});
	lock_table_[rid].lock_type_ = LockType::SHARED;
	return true;;
      }
    }
}

  bool LockManager::LockExclusive(Transaction *txn, const RID &rid) {
    return false;
  }

  bool LockManager::LockUpgrade(Transaction *txn, const RID &rid) {
    return false;
  }

  bool LockManager::Unlock(Transaction *txn, const RID &rid) {
    return false;
  }

} // namespace cmudb
