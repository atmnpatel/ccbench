#pragma once

#include <atomic>

#include "tuple.hpp"

#include "../../include/cache_line_size.hpp"
#include "../../include/int64byte.hpp"
#include "../../include/masstree_wrapper.hpp"
#include "../../include/random.hpp"

#ifdef GLOBAL_VALUE_DEFINE
  #define GLOBAL
  GLOBAL std::atomic<size_t> Running(0);
  alignas(CACHE_LINE_SIZE) GLOBAL uint64_t_64byte GlobalEpoch(1);
  #if MASSTREE_USE
    alignas(CACHE_LINE_SIZE) GLOBAL MasstreeWrapper<Tuple> MT;
  #endif
#else
  #define GLOBAL extern
  GLOBAL std::atomic<size_t> Running;
  alignas(CACHE_LINE_SIZE) GLOBAL uint64_t_64byte GlobalEpoch;
  #if MASSTREE_USE
    alignas(CACHE_LINE_SIZE) GLOBAL MasstreeWrapper<Tuple> MT;
  #endif
#endif

alignas(CACHE_LINE_SIZE) GLOBAL uint64_t_64byte *ThLocalEpoch;

// run-time args
GLOBAL size_t TUPLE_NUM;
GLOBAL size_t MAX_OPE;
GLOBAL size_t THREAD_NUM;
GLOBAL size_t RRATIO;
GLOBAL bool RMW;
GLOBAL double ZIPF_SKEW;
GLOBAL bool YCSB;
GLOBAL size_t CLOCKS_PER_US;
GLOBAL size_t EPOCH_TIME;
GLOBAL size_t EXTIME;

GLOBAL RWLock CtrLock;

// for logging emulation
alignas(CACHE_LINE_SIZE) GLOBAL uint64_t_64byte *Start;  
alignas(CACHE_LINE_SIZE) GLOBAL uint64_t_64byte *Stop;

alignas(CACHE_LINE_SIZE) GLOBAL Tuple *Table;

// 下記，ifdef で外すと lock.cc のコードから
// 参照出来なくてエラーが起きる．
// lock.hpp, lock.cc の全てを ifdef で分岐させるのは大変な労力なので，
// 行わず，これは RWLOCK モードでも宣言だけしておく．
alignas(CACHE_LINE_SIZE) GLOBAL MQLNode **MQLNodeTable;
// first dimension index number corresponds to the thread number.
// second dimension index number corresponds to the key of records.
// the element mean MQLnode whihch is owned by the thread which has the thread number corresponding to index number.
// for MQL sentinel value
// index 0 mean None.
// index 1 mean Acquired.
// index 2 mean SuccessorLeaving.
