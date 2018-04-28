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

  enum class LockType{ EMPTY, SHARED, EXCLUSIVE, UPDATE };

  struct TransactionInfo
  {
    Transaction* txn_;
    bool grated_;
    LockType lock_type_;
    std::promise<bool> promise_;
  }; 

  struct LockList
  {
    std::list<TransactionInfo> lock_list_; 
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
  
private:
  bool CheckForWaitDie(Transaction *txn, const RID &rid);
  
  bool strict_2PL_;

  std::unordered_map<RID, LockList> lock_table_;
  std::mutex lock_table_latch_;
  std::condition_variable condition_;
};

} // namespace cmudb
