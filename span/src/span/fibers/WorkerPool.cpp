#include "span/fibers/WorkerPool.hh"
#include "span/fibers/Fiber.hh"

#include "glog/logging.h"

namespace span {
  namespace fibers {
    WorkerPool::WorkerPool(size_t threads, bool useCaller, size_t batchSize)
      : Scheduler(threads, useCaller, batchSize) {
      start();
    }

    void WorkerPool::idle() {
      while (true) {
        if (Stopping()) {
          return;
        }
        sema.wait();
        try {
          Fiber::yield();
        } catch (...) {
          return;
        }
      }
    }

    void WorkerPool::tickle() {
      LOG(INFO) << this << " tickling";
      sema.notify();
    }
  }  // namespace fibers
}  // namespace span
