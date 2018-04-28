/**
 * lock_manager.cpp
 */

#include "concurrency/lock_manager.h"
#include <cassert>
#include <algorithm>
#include <list>

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
	|| res_itr->second->lock_list_.size() == 0) {
      // No lock is held, so we can require the lock
      if (res_itr == lock_table_.end()) {
	lock_table_.insert({rid, std::make_shared<LockList>()});
	res_itr = lock_table_.find(rid);
      }

      // we don't need promise on granted item
      res_itr->second->lock_list_.push_back({txn, true, LockType::SHARED, nullptr}); 
      txn->GetSharedLockSet()->insert(rid);
      return true;
    } else {
      // if there are some txns waiting while the state is SHARED
      if (res_itr->second->lock_list_.front().lock_type_ == LockType::SHARED) {
	// while some other one is also waiting for the lock
	if (!res_itr->second->lock_list_.back().grated_) {
	  bool ok = CheckForWaitDie(txn, rid);
	  if (ok) {
	    // follow the wait-die, so we just 
	    res_itr->second->lock_list_.push_back({txn, false, LockType::SHARED,
		  std::make_shared<std::promise<bool>>()});
	  } else {
	    txn->SetState(TransactionState::ABORTED);
	    return false;
	  }
	} else {
	  res_itr->second->lock_list_.push_back({txn, true, LockType::SHARED, nullptr});
	  txn->GetSharedLockSet()->insert(rid);
	  return true;
	}
      } else {
	if (res_itr->second->lock_list_.front().txn_->GetTransactionId() == txn->GetTransactionId()) {
	  
	  txn->GetSharedLockSet()->insert(rid);
	  return true;
	}
	
	bool ok = CheckForWaitDie(txn, rid);
	if (ok) {
	  res_itr->second->lock_list_.push_back({txn, false, LockType::SHARED,
		std::make_shared<std::promise<bool>>()});
	} else {
	  txn->SetState(TransactionState::ABORTED);
	  return false;
	}
      }
    }

    latch.unlock();
    // block for the waiting
    res_itr->second->lock_list_.end()->promise_->get_future().get();
    if (txn->GetState() == TransactionState::ABORTED) {
      // there is an abort happened
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
	|| res_itr->second->lock_list_.size() == 0) {
      // No lock is held, so we can require the lock
      if (res_itr == lock_table_.end()) {
	lock_table_.insert({rid, std::make_shared<LockList>()});
	res_itr = lock_table_.find(rid);
      }

      res_itr->second->lock_list_.push_back({txn, true, LockType::EXCLUSIVE, nullptr}); 

      txn->GetExclusiveLockSet()->insert(rid);
      return true;
    } else {

      // check for WAIT-DIE
      if (CheckForWaitDie(txn, rid)) {
	// wait for notifying
	res_itr->second->lock_list_.push_back({txn, false, LockType::EXCLUSIVE,\
	      std::make_shared<std::promise<bool>>()}); 
      } else {
	// break the WAIT_DIE fule
	txn->SetState(TransactionState::ABORTED);
	return false;
      }

      latch.unlock();
      // wait until being notified
      res_itr->second->lock_list_.end()->promise_->get_future().get();
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
    auto txn_info_itr = std::find_if(res_itr->second->lock_list_.begin(),
				     res_itr->second->lock_list_.end(),
				     [&](const TransactionInfo &elem) {
				       return elem.txn_ == txn;
				     });

    // for removing the warning
    if (txn_info_itr == res_itr->second->lock_list_.end())
      assert(txn_info_itr != res_itr->second->lock_list_.end());
    assert(txn_info_itr->grated_ == true && txn_info_itr->lock_type_ == LockType::SHARED);

    std::for_each(res_itr->second->lock_list_.begin(),
		  res_itr->second->lock_list_.end(),
		  [&](const TransactionInfo &elem) {
		    if (elem.grated_)
		      all_shared++;
		  });

    if (all_shared != 1) {
      if (CheckForWaitDie(txn, rid)) {
	res_itr->second->lock_list_.push_back({txn, false, LockType::UPDATE,
	      std::make_shared<std::promise<bool>>()});
	latch.unlock(); 
	// wait until notify
	res_itr->second->lock_list_.end()->promise_->get_future().get(); 
      } else {
	txn->SetState(TransactionState::ABORTED);
	return false;
      }
    } else {
      txn_info_itr->lock_type_ = LockType::EXCLUSIVE; 
    }

    // TODO(handora): this should be locked for RACE CONDITION
    txn->GetExclusiveLockSet()->insert(rid);
    return true;
  }

  bool LockManager::Unlock(Transaction *txn, const RID &rid) {
    std::unique_lock<std::mutex> latch(lock_table_latch_);

    // strict 2pl should be eithor committed or aborted
    assert(!strict_2PL_ || (txn->GetState() == TransactionState::COMMITTED || txn->GetState() != TransactionState::ABORTED));

    if (!strict_2PL_ && txn->GetState() == TransactionState::GROWING) {
      txn->SetState(TransactionState::SHRINKING); 
    }

    auto res_itr = lock_table_.find(rid);
    assert(res_itr != lock_table_.end());

    auto res_list = res_itr->second->lock_list_;
    auto txn_info_itr = std::find_if(res_list.begin(),
				     res_list.end(),
		 [&](const TransactionInfo& elem) {
                   return elem.grated_ == true && elem.txn_ == txn;
		 });

    // there is a case that read is erased from list
    if (txn_info_itr == res_list.end())
      return true;
      
    if (txn_info_itr->lock_type_ == LockType::EXCLUSIVE) {
      // get the next one for doing if not null
      auto next_txn_info_itr = txn_info_itr;
      next_txn_info_itr++;
      if (next_txn_info_itr != res_list.end()) {
	next_txn_info_itr->grated_ = true;
	// notify the next one
	next_txn_info_itr->promise_->set_value(true);

	if (next_txn_info_itr->lock_type_ == LockType::SHARED) {
	  next_txn_info_itr++;
	  while (next_txn_info_itr != res_list.end()
		 && next_txn_info_itr->lock_type_ == LockType::SHARED) {
	    next_txn_info_itr->grated_ = true;
	    // notify the next one
	    next_txn_info_itr->promise_->set_value(true); 
	  }
	}
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
	auto next_txn_info_itr = txn_info_itr;
	next_txn_info_itr++;
	if (next_txn_info_itr != res_list.end()) {
	  next_txn_info_itr->grated_ = true;
	  // notify the next one
	  next_txn_info_itr->promise_->set_value(true);

	  if (next_txn_info_itr->lock_type_ == LockType::SHARED) {
	    next_txn_info_itr++;
	    while (next_txn_info_itr != res_list.end()
		   && next_txn_info_itr->lock_type_ == LockType::SHARED) {
	      next_txn_info_itr->grated_ = true;
	      // notify the next one
	      next_txn_info_itr->promise_->set_value(true); 
	    }
	  }
	}

      } else if (shared_num == 2) {
	auto next_txn_info_itr = txn_info_itr;
	next_txn_info_itr++;
	if (next_txn_info_itr != res_list.end() &&
	    next_txn_info_itr->lock_type_ == LockType::UPDATE) {
	  auto update_txn = find_if(res_list.begin(), res_list.end(),
				    [&](const TransactionInfo&elem) {
				      return elem.txn_ == next_txn_info_itr->txn_;
				    });
	  assert(update_txn != res_list.end());
	  res_list.erase(update_txn);
	  next_txn_info_itr->grated_ = true;
	  next_txn_info_itr->lock_type_ = LockType::EXCLUSIVE;
	  next_txn_info_itr->promise_->set_value(true); 
	}
      }
    }

    
    res_list = lock_table_.find(rid)->second->lock_list_; 
    
    return true;
  }

  bool LockManager::CheckForWaitDie(Transaction *txn, const RID &rid) {
    for (const auto &ele: lock_table_[rid]->lock_list_) {
      if (ele.txn_->GetTransactionId() < txn->GetTransactionId()) {
	return false;
      }
    }
    return true;
  }

/*
================================================================
                           TEST ONLY
================================================================
*/

  bool LockManager::IsClean() {
    // RAII latch protect the lock table for concurrent visits
    std::unique_lock<std::mutex> latch(lock_table_latch_);
    
    for (const auto &list: lock_table_) {
      if (list.second->lock_list_.size() != 0) {
	return false;
      }
    }
    return true;
  }

  bool LockManager::IsClean(Transaction *txn) {
    // RAII latch protect the lock table for concurrent visits
    std::unique_lock<std::mutex> latch(lock_table_latch_);
    
    for (const auto &list: lock_table_) {
      auto found = std::find_if(list.second->lock_list_.begin(),
				list.second->lock_list_.end(),
				[&](const TransactionInfo& elem) {
				  return elem.txn_ == txn;
				});
      if (found != list.second->lock_list_.end()) {
	return false;
      }
    }
    return true;
  }

  void LockManager::ShowLock() {
    // RAII latch protect the lock table for concurrent visits
    std::unique_lock<std::mutex> latch(lock_table_latch_);
    
    for (const auto &list: lock_table_) {
      std::cout << list.first;
      std::for_each(list.second->lock_list_.begin(),
		    list.second->lock_list_.end(),
			[&](const TransactionInfo& elem) {
		      std::cout << "\t" << elem.txn_->GetTransactionId() << /*"(" << elem.txn_->GetState() << ")" <<*/ std::endl;
		    }); 
    } 
  }
} // namespace cmudb
