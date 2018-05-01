/**
 * lock_manager.cpp
 * TODO(Handora): use two list(one wait list and one granted list for simplicity)
 * TODO(Handora): use oldest_timestamp for speed
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

    // aborted txn should not lock again
    if (txn->GetState() == TransactionState::ABORTED) {
      return false;
    }

    auto res_itr = lock_table_.find(rid);
    if (res_itr == lock_table_.end() 
	|| res_itr->second->lock_list_.size() == 0) {
      // No lock is held, so we can require the lock
      if (res_itr == lock_table_.end()) {
	lock_table_.insert({rid, std::make_shared<LockList>()});
	// find again, so we can locate the true location
	res_itr = lock_table_.find(rid);
      }

      res_itr->second->list_lock_type_ = LockType::SHARED;
      // we don't need promise on granted item
      res_itr->second->lock_list_.push_back({txn, true, LockType::SHARED, nullptr});
      // get one granted 
      res_itr->second->list_grant_cnt_++;
      txn->GetSharedLockSet()->insert(rid);
      return true;
    } else {
      // if there are some txns waiting while the state is SHARED
      if (res_itr->second->list_lock_type_ == LockType::SHARED) {
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
	  // all lock is granted 
	  res_itr->second->lock_list_.push_back({txn, true, LockType::SHARED, nullptr});
	  txn->GetSharedLockSet()->insert(rid);
	  res_itr->second->list_grant_cnt_++;
	  return true;
	}
      } else {
	// the first one must be the exclusive one
        auto extensive_ptr = res_itr->second->lock_list_.begin(); 
	assert(extensive_ptr != res_itr->second->lock_list.end()
	       && extensive_ptr->grated_ == true
	       && extensive_ptr->lock_type_ == LockType::EXCLUSIVE);

	if (extensive_ptr->txn_ == txn) {
	  // TODO(Handora): should we insert this txn to the list
	  // res_itr->second->list_grant_cnt_++;
	  LOG_DEBUG("Reentrant lock");
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

    auto wait_ptr = res_itr->second->lock_list_.end();
    latch.unlock();
    // block for the waiting
    // what the unlock should do is wake up and put the txn in the right place
    wait_ptr->promise_->get_future().get();
    
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
	// renew the res_itr
	res_itr = lock_table_.find(rid);
      }

      res_itr->second->list_lock_type_ = LockType::EXCLUSIVE; 
      res_itr->second->lock_list_.push_back({txn, true, LockType::EXCLUSIVE, nullptr});
      res_itr->second->list_grant_cnt_++;

      txn->GetExclusiveLockSet()->insert(rid);
      return true;
    } else {

      // TODO(Handora): should we care about reentrant lock
      
      if (res_itr->second->list_lock_type_ == LockType::EXCLUSIVE) {
      	auto extensive_ptr = res_itr->second->lock_list_.begin();
      	assert(extensive_ptr != res_itr->second->lock_list.end()
      	       && extensive_ptr->grated_ == true
      	       && extensive_ptr->lock_type_ == LockType::EXCLUSIVE);

      	if (extensive_ptr->txn_ == txn) {
      	  // res_itr->second->list_grant_cnt_++;
	  LOG_DEBUG("Reentrant lock");
	  return true;
      	}
      }
      
      // check for WAIT-DIE
      if (CheckForWaitDie(txn, rid)) {
	// wait for notifying
	res_itr->second->lock_list_.push_back({txn, false, LockType::EXCLUSIVE,	\
	      std::make_shared<std::promise<bool>>()}); 
      } else {
	// break the WAIT_DIE fule
	txn->SetState(TransactionState::ABORTED);
	return false;
      }

      auto wait_ptr = res_itr->second->lock_list_.end();
      latch.unlock();
      // block for the waiting
      // what the unlock should do is wake up and put the txn in the right place
      wait_ptr->promise_->get_future().get();
      
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

    auto res_list = res_itr->second->lock_list_;
    assert(res_itr->second->lock_list_.size() != 0);

    if (res_itr->second->list_lock_type_ == LockType::SHARED) {
      if (res_itr->second->list_grant_cnt_ == 1) {
	assert(res_list.begin()->txn_ == txn);
	res_list.begin()->lock_type_ = LockType::EXCLUSIVE;
	// res_itr->second->list_grant_cnt_++;
	txn->GetSharedLockSet()->erase(rid);
	txn->GetExclusiveLockSet()->insert(rid);
	return true;
      }
    }
    
    // x y x or y x x
    // x y x is not possible while y x x is possible

    if (CheckForWaitDie(txn, rid)) {
      res_list.push_back({txn, false, LockType::EXCLUSIVE,\
	    std::make_shared<std::promise<bool>>()});
    } else {
      txn->SetState(TransactionState::ABORTED);
      return false;
    }

    auto wait_ptr = res_itr->second->lock_list_.end();
    latch.unlock();
    // block for the waiting
    // what the unlock should do is wake up and put the txn in the right place
    wait_ptr->promise_->get_future().get();
      
    if (txn->GetState() == TransactionState::ABORTED) {
      return false;
    }

    txn->GetExclusiveLockSet()->insert(rid);
    txn->GetSharedLockSet()->erase(rid);
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
    
    assert(txn_info_itr == res_list.end());
    res_itr->second->list_grant_cnt_--;
    auto lock_type = txn_info_itr->lock_type_;
    txn_info_itr->lock_type_ = LockType::DELETE;
    if (lock_type == LockType::EXCLUSIVE) {
      std::cout << "EXclusive" << std::endl;
      flush(std::cout);
      // get the next one for doing if not null
      assert(res_itr->second->list_grant_cnt_ == 0);
      auto next_txn_info_itr = txn_info_itr;
      next_txn_info_itr++;
      
      if (next_txn_info_itr != res_list.end()) {
	next_txn_info_itr->grated_ = true;
	// notify the next one
	next_txn_info_itr->promise_->set_value(true);
	res_itr->second->list_grant_cnt_++;
	res_itr->second->list_lock_type_ = next_txn_info_itr->lock_type_;
	if (next_txn_info_itr->lock_type_ == LockType::SHARED) {
	  auto next_next_txn_info_itr = next_txn_info_itr;
	  next_next_txn_info_itr++;

	  if (next_next_txn_info_itr != res_list.end() 
	      && next_next_txn_info_itr->lock_type_ == LockType::EXCLUSIVE) {
	    if (next_next_txn_info_itr->txn_ == next_txn_info_itr->txn_) {
	      // the update part
	      // TODO(Handora): may be there will be a race condition
	      res_itr->second->list_lock_type_ = LockType::EXCLUSIVE; 
	      next_txn_info_itr->lock_type_ = LockType::DELETE;
	    } 
	  } else {
	    next_txn_info_itr++;
	    
	    while (next_txn_info_itr != res_list.end()
		   && next_txn_info_itr->lock_type_ == LockType::SHARED) {
	      next_txn_info_itr->grated_ = true;
	      // notify the next one
	      res_itr->second->list_grant_cnt_++;
	      next_txn_info_itr->promise_->set_value(true); 
	    }
	  }
	}
      }
    } else if (lock_type == LockType::SHARED) {
      std::cout << "Shared" << std::endl;
      flush(std::cout);
      
      if (res_itr->second->list_grant_cnt_ == 0) {
	std::cout << "Shared1" << std::endl;
	flush(std::cout);

	// get the next one for doing if not null
	assert(res_itr->second->list_grant_cnt_ == 0);
	auto next_txn_info_itr = txn_info_itr;
	next_txn_info_itr++;
      
	if (next_txn_info_itr != res_list.end()) {
	  next_txn_info_itr->grated_ = true;
	  // notify the next one
	  next_txn_info_itr->promise_->set_value(true);
	  res_itr->second->list_grant_cnt_++;
	  res_itr->second->list_lock_type_ = next_txn_info_itr->lock_type_;
	  if (next_txn_info_itr->lock_type_ == LockType::SHARED) {
	    auto next_next_txn_info_itr = next_txn_info_itr;
	    next_next_txn_info_itr++;

	    if (next_next_txn_info_itr != res_list.end() 
		&& next_next_txn_info_itr->lock_type_ == LockType::EXCLUSIVE) {
	      if (next_next_txn_info_itr->txn_ == next_txn_info_itr->txn_) {
		// the update part
		// TODO(Handora): may be there will be a race condition
		res_itr->second->list_lock_type_ = LockType::EXCLUSIVE; 
		next_txn_info_itr->lock_type_ = LockType::DELETE;
	      } 
	    } else {
	      next_txn_info_itr++;
	    
	      while (next_txn_info_itr != res_list.end()
		     && next_txn_info_itr->lock_type_ == LockType::SHARED) {
		next_txn_info_itr->grated_ = true;
		// notify the next one
		res_itr->second->list_grant_cnt_++;
		next_txn_info_itr->promise_->set_value(true); 
	      }
	    }
	  }
	}
      } else if (res_itr->second->list_grant_cnt_ == 1) {
	std::cout << "Shared2" << std::endl;
	flush(std::cout);
	
	auto next_txn_info_itr = txn_info_itr;
	next_txn_info_itr++;
	if (next_txn_info_itr != res_list.end()
	    && next_txn_info_itr->lock_type_ == LockType::EXCLUSIVE) {
	  std::cout << "update" << std::endl;
	  flush(std::cout);
	  auto update_txn = find_if(res_list.begin(), res_list.end(),
				    [&](const TransactionInfo&elem) {
				      return elem.txn_ == next_txn_info_itr->txn_;
				    });
          update_txn->lock_type_ = LockType::EXCLUSIVE;
          
	  next_txn_info_itr->grated_ = true;
	  next_txn_info_itr->lock_type_ = LockType::EXCLUSIVE;
	  res_itr->second->list_lock_type_ = LockType::EXCLUSIVE; 
	  next_txn_info_itr->promise_->set_value(true); 
	}
      }
    }
    

    res_list.remove_if([](const TransactionInfo& elem) {
	return elem.lock_type_ == LockType::DELETE;
      });
    
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
