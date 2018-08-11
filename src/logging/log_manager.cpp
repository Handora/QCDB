/**
 * log_manager.cpp
 */

#include "logging/log_manager.h"

namespace cmudb {
/*
 * set ENABLE_LOGGING = true
 * Start a separate thread to execute flush to disk operation periodically
 * The flush can be triggered when the log buffer is full or buffer pool
 * manager wants to force flush (it only happens when the flushed page has a
 * larger LSN than persistent LSN)
 */
void LogManager::RunFlushThread() {
  ENABLE_LOGGING = true;
  flush_thread_ = new std::thread([&]() {
	std::unique_lock<std::mutex> latch(latch_);

	for (;;) {
	  cv_.wait_for(latch, LOG_TIMEOUT);
	  if (ENABLE_LOGGING == false) {
		return;
	  }

	  disk_manager_->WriteLog(flush_buffer_, flush_offset_);
	}
  });
}
/*
 * Stop and join the flush thread, set ENABLE_LOGGING = false
 */
void LogManager::StopFlushThread() {
  ENABLE_LOGGING = false;

  {
	std::unique_lock<std::mutex> latch(latch_);
	cv_.notify_one();
  }

  flush_thread_->join();
  delete flush_thread_;
}

/*
 * append a log record into log buffer
 * you MUST set the log record's lsn within this method
 * @return: lsn that is assigned to this log record
 *
 *
 * example below
 * // First, serialize the must have fields(20 bytes in total)
 * log_record.lsn_ = next_lsn_++;
 * memcpy(log_buffer_ + offset_, &log_record, 20);
 * int pos = offset_ + 20;
 *
 * if (log_record.log_record_type_ == LogRecordType::INSERT) {
 *    memcpy(log_buffer_ + pos, &log_record.insert_rid_, sizeof(RID));
 *    pos += sizeof(RID);
 *    // we have provided serialize function for tuple class
 *    log_record.insert_tuple_.SerializeTo(log_buffer_ + pos);
 *  }
 *
 */
lsn_t LogManager::AppendLogRecord(LogRecord &log_record) {

  log_record.lsn_ = next_lsn_++;

  if (!SafetyForAppend(log_record)) {
	std::unique_lock<std::mutex> latch(latch_);

	std::swap(log_buffer_, flush_buffer_);
	flush_offset_ = log_offset_;
	log_offset_ = 0;
	cv_.notify_one();
  }

  memcpy(log_buffer_ + log_offset_, &log_record, 20);
  // int pos = log_offset_ + 20;


  return INVALID_LSN;
}

bool LogManager::SafetyForAppend(LogRecord &log_record) {
  int left_size = LOG_BUFFER_SIZE - log_offset_;
  int need_size = 0;

  assert(log_record.GetLogRecordType() != LogRecordType::INVALID);
  switch (log_record.GetLogRecordType()) {
	case LogRecordType::INSERT:
	case LogRecordType::MARKDELETE:
	case LogRecordType::APPLYDELETE:
	case LogRecordType::ROLLBACKDELETE: need_size = 24 + sizeof(RID) + log_record.GetSize();
	  break;
	case LogRecordType::UPDATE:
	  need_size = 28 + sizeof(RID) + log_record.old_tuple_.GetLength()
		  + log_record.new_tuple_.GetLength();
	  break;
	case LogRecordType::NEWPAGE: need_size = 24;
	  break;
	default:need_size = 20;
  }

  return need_size <= left_size;
}

} // namespace cmudb
