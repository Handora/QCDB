/**
 * lock_manager.h
 *
 * Tuple level lock manager, use wait-die to prevent deadlocks
 */

#pragma once

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <future>

#include "common/rid.h"
#include "concurrency/transaction.h"

namespace cmudb {

class LockManager {

  enum class LockType{ EMPTY, SHARED, EXCLUSIVE, UPDATE, DELETE };

  struct TransactionInfo
  {
    Transaction* txn_;
    bool grated_;
    LockType lock_type_;
    std::unique_ptr<std::promise<bool>> promise_;
  }; 

  struct LockList
  {
    std::list<TransactionInfo> lock_list_;
    LockList(): list_lock_type_(LockType::EMPTY),
		list_grant_cnt_(0) {};
    LockType list_lock_type_;
    int list_grant_cnt_;
  };

public:
  LockManager(bool strict_2PL) : strict_2PL_(strict_2PL){};

  /*** below are APIs need to implement ***/
  // lock:
  // return false if transaction is aborted
  // it should be blocked on waiting and should return true when granted
  // note the behavior of trying to lock locked rids by same txn is undefined
  // it is transaction's job to keep track of its current locks
  bool LockShared(Transaction *txn, const RID &rid);
  bool LockExclusive(Transaction *txn, const RID &rid);
  bool LockUpgrade(Transaction *txn, const RID &rid);

  // unlock:
  // release the lock hold by the txn
  bool Unlock(Transaction *txn, const RID &rid);
  /*** END OF APIs ***/

  // test only
  // is there are no lock is granted or waiting for granting
  bool IsClean();
  bool IsClean(Transaction *txn);
  void ShowLock();
  void SayType(LockType type) {
    if (type == LockType::SHARED) {
      std::cout << "SHARED";
    } else if (type == LockType::EXCLUSIVE) {
      std::cout << "EXCLUSIVE";
    } else if (type == LockType::DELETE) {
      std::cout << "DELETE";
    } else {
      std::cout << "May be empty"; 
    }
  }
  
private:
  bool CheckForWaitDie(Transaction *txn, const RID &rid);
  
  bool strict_2PL_;

  std::unordered_map<RID, std::shared_ptr<LockList>> lock_table_;
  std::mutex lock_table_latch_; 
};

} // namespace cmudb
