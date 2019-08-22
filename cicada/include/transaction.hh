#pragma once

#include <stdio.h>
#include <atomic>
#include <iostream>
#include <map>
#include <queue>

#include "../../include/config.hh"
#include "../../include/debug.hh"
#include "../../include/inline.hh"
#include "../../include/procedure.hh"
#include "../../include/result.hh"
#include "../../include/string.hh"
#include "../../include/util.hh"
#include "cicada_op_element.hh"
#include "common.hh"
#include "time_stamp.hh"
#include "tuple.hh"
#include "version.hh"

#define CONTINUING_COMMIT_THRESHOLD 5

enum class TransactionStatus : uint8_t {
  invalid,
  inflight,
  commit,
  abort,
};

class TxExecutor {
 public:
  TransactionStatus status_ = TransactionStatus::invalid;
  TimeStamp wts_;
  std::vector<ReadElement<Tuple>> read_set_;
  std::vector<WriteElement<Tuple>> write_set_;
  std::deque<GCElement<Tuple>> gcq_;
  std::deque<Version*> reuse_version_from_gc_;
  std::vector<Procedure> pro_set_;
  Result* cres_ = nullptr;

  bool ronly_;
  uint8_t thid_ = 0;
  uint64_t rts_;
  uint64_t start_, stop_;                // for one-sided synchronization
  uint64_t grpcmt_start_, grpcmt_stop_;  // for group commit
  uint64_t gcstart_, gcstop_;            // for garbage collection

  char return_val_[VAL_SIZE] = {};
  char write_val_[VAL_SIZE] = {};

  TxExecutor(uint8_t thid, Result* cres) : cres_(cres), thid_(thid) {
    // wait to initialize MinWts
    while (MinWts.load(memory_order_acquire) == 0)
      ;
    rts_ = MinWts.load(memory_order_acquire) - 1;
    wts_.generateTimeStampFirst(thid_);

    __atomic_store_n(&(ThreadWtsArray[thid_].obj_), wts_.ts_, __ATOMIC_RELEASE);
    unsigned int expected, desired;
    expected = FirstAllocateTimestamp.load(memory_order_acquire);
    for (;;) {
      desired = expected + 1;
      if (FirstAllocateTimestamp.compare_exchange_weak(expected, desired,
                                                       memory_order_acq_rel))
        break;
    }

    read_set_.reserve(MAX_OPE);
    write_set_.reserve(MAX_OPE);
    pro_set_.reserve(MAX_OPE);

    if (PRE_RESERVE_VERSION) {
      reuse_version_from_gc_.resize(PRE_RESERVE_VERSION);
      reuse_version_from_gc_.clear();
      Version* ver;
      if (posix_memalign((void**)&ver, PAGE_SIZE,
                         PRE_RESERVE_VERSION * sizeof(Version)))
        ERR;
      for (size_t i = 0; i < PRE_RESERVE_VERSION; ++i)
        reuse_version_from_gc_.emplace_back(&ver[i]);
    }

    genStringRepeatedNumber(write_val_, VAL_SIZE, thid_);

    start_ = rdtscp();
    gcstart_ = start_;
  }

  void abort();
  bool chkGcpvTimeout();
  void cpv();  // commit pending versions
  void displayWriteSet();
  void earlyAbort();

  void gcAfterThisVersion(const Tuple *tuple, Version* delTarget) {
    while (delTarget != nullptr) {
      // escape next pointer
      Version *tmp = delTarget->next_.load(std::memory_order_acquire);

#if INLINE_VERSION_OPT
      if (delTarget == &(tuple->inline_version_)) {
        gcq_.front().rcdptr_->returnInlineVersionRight();
      } else {
#if REUSE_VERSION
        reuse_version_from_gc_.emplace_back(delTarget);
#else // if REUSE_VERSION
        delete delTarget;
#endif // if REUSE_VERSION
      }
#else // if INLINE_VERSION_OPT

#if REUSE_VERSION
      reuse_version_from_gc_.emplace_back(delTarget);
#else // if REUSE_VERSION
      delete delTarget;
#endif // if REUSE_VERSION
#endif // if INLINE_VERSION_OPT
#if ADD_ANALYSIS
      ++cres_->local_gc_version_counts_;
#endif
      delTarget = tmp;
    }
  }

  void gcpv();    // group commit pending versions

  void inlineVersionPromotion(const uint64_t key, Tuple* tuple, Version* version) {
    if (version != &(tuple->inline_version_) &&
        MinRts.load(std::memory_order_acquire) > version->ldAcqWts() &&
        tuple->inline_version_.status_.load(std::memory_order_acquire) ==
            VersionStatus::unused) {
      twrite(key);
      if ((*pro_set_.begin()).ronly_) {
        (*pro_set_.begin()).ronly_ = false;
        read_set_.emplace_back(key, tuple, version);
      }
    }
  }

  void mainte();  // maintenance

  Version* newVersionGeneration() {
#if REUSE_VERSION
  if (reuse_version_from_gc_.empty()) {
#if ADD_ANALYSIS
    ++cres_->local_version_malloc_;
#endif
    return new Version(0, this->wts_.ts_);
  } else {
#if ADD_ANALYSIS
    ++cres_->local_version_reuse_;
#endif
    Version* newVersion = reuse_version_from_gc_.back();
    reuse_version_from_gc_.pop_back();
    newVersion->set(0, this->wts_.ts_);
    return newVersion;
  }
#else
#if ADD_ANALYSIS
  ++cres_->local_version_malloc_;
#endif
  return new Version(0, this->wts_.ts_);
#endif
  }

  bool precheckInValidation() {
    // Two optimizations can add unnecessary overhead under low contention
    // because they do not improve the performance of uncontended workloads.
    // Each thread adaptively omits both steps if the recent transactions have
    // been committed (5 in a row in our implementation).
    //
    // Sort write set by contention
    partial_sort(write_set_.begin(),
                 write_set_.begin() + (write_set_.size() / 2),
                 write_set_.end());

    // Pre-check version consistency
    // (b) every currently visible version v of the records in the write set
    // satisfies (v.rts) <= (tx.ts)
    for (auto itr = write_set_.begin();
         itr != write_set_.begin() + (write_set_.size() / 2); ++itr) {
      if ((*itr).rcdptr_->continuing_commit_.load(memory_order_acquire) < CONTINUING_COMMIT_THRESHOLD) {
        Version *version = (*itr).rcdptr_->ldAcqLatest()->skipNotTheStatusVersionAfterThis(VersionStatus::committed, false);
        if ((*itr).rmw_ == false) {
          while (version->ldAcqWts() > this->wts_.ts_
              || version->ldAcqStatus() != VersionStatus::committed)
            version = version->ldAcqNext();
        }

        if (version->ldAcqRts() > this->wts_.ts_) return false;
      }
    }

    return true;
  }

  void precpv();  // pre-commit pending versions
  void pwal();    // parallel write ahead log.

  void readTimestampUpdateInValidation() {
    for (auto itr = read_set_.begin(); itr != read_set_.end(); ++itr) {
      uint64_t expected;
      expected = (*itr).ver_->ldAcqRts();
      for (;;) {
        if (expected > this->wts_.ts_) break;
        if ((*itr).ver_->rts_.compare_exchange_strong(expected, this->wts_.ts_,
                                                      memory_order_acq_rel,
                                                      memory_order_acquire))
          break;
      }
    }
  }

  void resetContinuingCommitInReadWriteSet() {
    for (auto itr = read_set_.begin(); itr != read_set_.end(); ++itr)
      (*itr).rcdptr_->continuing_commit_.store(0, memory_order_release);
    for (auto itr = write_set_.begin(); itr != write_set_.end(); ++itr)
      (*itr).rcdptr_->continuing_commit_.store(0, memory_order_release);
  }

  ReadElement<Tuple>* searchReadSet(const uint64_t key) {
    for (auto itr = read_set_.begin(); itr != read_set_.end(); ++itr) {
      if ((*itr).key_ == key) return &(*itr);
    }
    
    return nullptr;
  }

  WriteElement<Tuple>* searchWriteSet(const uint64_t key) {
    for (auto itr = write_set_.begin(); itr != write_set_.end(); ++itr) {
      if ((*itr).key_ == key) return &(*itr);
    }

    return nullptr;
  }

  void swal();
  void tbegin();
  char* tread(const uint64_t key);
  void twrite(const uint64_t key);
  bool validation();
  void writePhase();

  void writeSetClean() {
    for (auto itr = write_set_.begin(); itr != write_set_.end(); ++itr) {
      if ((*itr).finish_version_install_) {
        (*itr).newObject_->status_.store(VersionStatus::aborted,
                                         std::memory_order_release);
        continue;
      }

#if INLINE_VERSION_OPT
      if ((*itr).newObject_ == &(*itr).rcdptr_->inline_version_) {
        (*itr).rcdptr_->returnInlineVersionRight();
        continue;
      }
#endif

#if REUSE_VERSION
      reuse_version_from_gc_.emplace_back((*itr).newObject_);
#else
      delete (*itr).newObject_;
#endif
    }
    write_set_.clear();
  }

  static INLINE Tuple* get_tuple(Tuple* table, uint64_t key) {
    return &table[key];
  }
};
