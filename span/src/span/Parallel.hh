#ifndef SPAN_SRC_SPAN_PARALLEL_HH_
#define SPAN_SRC_SPAN_PARALLEL_HH_

#include <atomic>
#include <exception>
#include <memory>
#include <vector>

#include "span/fibers/Fiber.hh"
#include "span/fibers/Scheduler.hh"

#include "absl/synchronization/mutex.h"
#include "glog/logging.h"

namespace span {
  /**
   * Execute multiple functors in parallel.
   *
   * @param dgs
   *  The functors to execute.
   * @param parallelism
   *  How many dgs could be executed in parallel at most.
   *  NOTE: By default, all the dgs would be scheduled together and run with whatever concurrency is available
   *    from the scheduler. If parallelism > 0 only at most parallelism dgs could be scheduled into scheduler
   *    with later dgs not invoked until earlier dgs have completed.
   *
   * Execute multiple functors in parallel by scheduling them all on the current Scheduler. Concurrency is achieved
   * either because the Scheduler is running on multiple threads, or because the functors will yield to the scheduler
   * during execution, instead of blocking.
   *
   * If there is no scheduler associated with the current thread, the functors are simply executoed sequentially.
   *
   * If any of the functors throw an uncaught exception, the first uncaught exception is rethrown to the caller.
   */
  void parallel_do(const std::vector<std::function<void()>> *dgs, int parallelism = -1);

  void parallel_do(const std::vector<std::function<void()>> *dgs, std::vector<span::fibers::Fiber::ptr> *fibers,
    int parallelism = -1);

  namespace Detail {
    template<class Iterator, class Functor>
    static void parallel_foreach_impl(Iterator *begin, Iterator *end, Functor *functor, absl::Mutex *mutex,
      std::exception_ptr *exception, span::fibers::Scheduler *scheduler, span::fibers::Fiber::ptr caller,
      std::atomic<int> *count) {
      while (true) {
        try {
          Iterator it;
          {
            absl::MutexLock _lock(mutex);
            if (begin == end || *exception) {
              break;
            }
            it = begin++;
          }
          *functor(*it);
        } catch (...) {
          absl::MutexLock _lock(mutex);
          *exception = std::current_exception();
          break;
        }
      }

      // Don't want to own the mutex here cause another thread could pick up caller immediately.
      // and return from parallel_for before this thread has a chance to unlock it.
      if ((--count) == 0) {
        scheduler->schedule(caller);
      }
    }
  }  // namespace Detail

  template<class Iterator, class Functor>
  void parallel_foreach(Iterator begin, Iterator end, Functor functor, int parallelism = -1) {
    if (parallelism == -1) {
      parallelism = 4;
    }
    span::fibers::Scheduler *scheduler = span::fibers::Scheduler::getThis();

    if (parallelism == 1 || !scheduler) {
      DLOG(INFO) << "running parallel_for sequentially";
      while (begin != end) {
        functor(*begin++);
      }
      return;
    }

    absl::Mutex mutex;
    std::exception_ptr exception;

    DLOG(INFO) << "running parallel_for with " << parallelism << " fibers";
    std::atomic<int> count(parallelism);
    for (int idx = 0; idx < parallelism; ++idx) {
      scheduler->schedule(std::bind(&Detail::parallel_foreach_impl<Iterator, Functor>,
        &begin, &end, &functor, &mutex, &exception, scheduler, span::fibers::Fiber::getThis(), &count));
    }
    span::fibers::Scheduler::yieldTo();

    if (exception) {
      throw exception;
    }
  }
}  // namespace span

#endif  // SPAN_SRC_SPAN_PARALLEL_HH_
