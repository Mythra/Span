#include "span/Parallel.hh"

#include <atomic>
#include <memory>
#include <vector>

#include "span/exceptions/Assert.hh"
#include "span/fibers/FiberSynchronization.hh"

namespace span {
  static void parallel_do_impl(std::function<void()> dg, std::atomic<size_t> *completed, size_t total,
    std::exception_ptr *exception, span::fibers::Scheduler *scheduler, span::fibers::Fiber::ptr caller,
    span::fibers::FiberSemaphore *sem) {
    if (sem) {
      sem->wait();
    }
    try {
      dg();
    } catch(...) {
      *exception = std::current_exception();
    }
    if (sem) {
      sem->notify();
    }
    if ((++*completed) == total) {
      scheduler->schedule(caller);
    }
  }

  void parallel_do(const std::vector<std::function<void()>> *dgs, int parallelism) {
    std::atomic<size_t> completed(0);
    span::fibers::Scheduler *scheduler = span::fibers::Scheduler::getThis();
    span::fibers::Fiber::ptr caller = span::fibers::Fiber::getThis();
    std::vector<std::function<void()>>::const_iterator it;

    if (scheduler == NULL || dgs->size() <= 1) {
      for (it = dgs->begin(); it != dgs->end(); ++it) {
        (*it)();
      }
      return;
    }

    SPAN_ASSERT(parallelism != 0);
    std::shared_ptr<span::fibers::FiberSemaphore> sem;
    if (parallelism != -1) {
      sem.reset(new span::fibers::FiberSemaphore(parallelism));
    }

    std::vector<span::fibers::Fiber::ptr> fibers;
    std::vector<std::exception_ptr> exceptions;
    fibers.reserve(dgs->size());
    exceptions.resize(dgs->size());
    for (size_t idx = 0; idx < dgs->size(); ++idx) {
      span::fibers::Fiber::ptr f(new span::fibers::Fiber(std::bind(&parallel_do_impl,
        dgs->at(idx), &completed, dgs->size(), &exceptions[idx], scheduler, caller, sem.get())));
      fibers.push_back(f);
      scheduler->schedule(f);
    }

    span::fibers::Scheduler::yieldTo();
    for (std::vector<std::exception_ptr>::iterator it2 = exceptions.begin(); it2 != exceptions.end(); ++it2) {
      if (*it2) {
        throw *it2;
      }
    }
  }

  void parallel_do(const std::vector<std::function<void()>> *dgs, std::vector<span::fibers::Fiber::ptr> *fibers,
    int parallelism) {
    SPAN_ASSERT(fibers->size() >= dgs->size());

    std::atomic<size_t> completed(0);
    span::fibers::Scheduler *scheduler = span::fibers::Scheduler::getThis();;
    span::fibers::Fiber::ptr caller = span::fibers::Fiber::getThis();
    std::vector<std::function<void()>>::const_iterator it;

    if (scheduler == NULL || dgs->size() <= 1) {
      for (it = dgs->begin(); it != dgs->end(); ++it) {
        (*it)();
      }
      return;
    }

    std::shared_ptr<span::fibers::FiberSemaphore> sem;
    SPAN_ASSERT(parallelism != 0);
    if (parallelism != -1) {
      sem.reset(new span::fibers::FiberSemaphore(parallelism));
    }

    std::vector<std::exception_ptr> exceptions;
    exceptions.resize(dgs->size());

    for (size_t idx = 0; idx < dgs->size(); ++idx) {
      fibers->at(idx)->reset(std::bind(&parallel_do_impl, dgs->at(idx), &completed, dgs->size(),
        &exceptions[idx], scheduler, caller, sem.get()));
      scheduler->schedule(fibers->at(idx));
    }
    span::fibers::Scheduler::yieldTo();

    // Make sure all fibers have actually exited, to avoid the caller
    // immediately calling Fiber::reset, and the fiber hasn't actually exited
    // because it is running in a different thread.
    for (size_t idx = 0; idx < dgs->size(); ++idx) {
      while (fibers->at(idx)->state() == span::fibers::Fiber::EXEC) {
        span::fibers::Scheduler::yieldTo();
      }
    }

    for (size_t idx = 0; idx < dgs->size(); ++idx) {
      if (exceptions.at(idx)) {
        throw exceptions.at(idx);
      }
    }
  }
}  // namespace span
