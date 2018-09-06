#include "span/fibers/FiberSynchronization.hh"

#include <list>
#include <utility>

#include "span/exceptions/Assert.hh"
#include "span/fibers/Fiber.hh"
#include "span/fibers/Scheduler.hh"

#include "absl/synchronization/mutex.h"

namespace span {
  namespace fibers {
    FiberMutex::~FiberMutex() noexcept(false) {
    }

    void FiberMutex::lock() {
      SPAN_ASSERT(Scheduler::getThis());
      {
        absl::MutexLock _lock(&mutex_);
        SPAN_ASSERT(owner_ != Fiber::getThis());
        if (!owner_) {
          owner_ = Fiber::getThis();
          return;
        }
        waiters_.push_back(std::make_pair(Scheduler::getThis(), Fiber::getThis()));
      }
      Scheduler::yieldTo();
    }

    void FiberMutex::unlock() {
      absl::MutexLock _lock(&mutex_);
      unlockNoLock();
    }

    bool FiberMutex::unlockIfNotUnique() {
      absl::MutexLock _lock(&mutex_);
      SPAN_ASSERT(owner_ == Fiber::getThis());
      if (!waiters_.empty()) {
        unlockNoLock();
        return true;
      }
      return false;
    }

    void FiberMutex::unlockNoLock() {
      SPAN_ASSERT(owner_ == Fiber::getThis());
      owner_.reset();
      if (!waiters_.empty()) {
        std::pair<Scheduler *, Fiber::ptr> next = waiters_.front();
        waiters_.pop_front();
        owner_ = next.second;
        next.first->schedule(next.second);
      }
    }

    FiberSemaphore::FiberSemaphore(size_t initialConcurrency) : concurrency_(initialConcurrency) {
    }

    FiberSemaphore::~FiberSemaphore() noexcept(false) {
    }

    void FiberSemaphore::wait() {
      SPAN_ASSERT(Scheduler::getThis());
      {
        absl::MutexLock _lock(&mutex_);
        if (concurrency_ > 0u) {
          --concurrency_;
          return;
        }
        waiters_.push_back(std::make_pair(Scheduler::getThis(), Fiber::getThis()));
      }
      Scheduler::yieldTo();
    }

    void FiberSemaphore::notify() {
      absl::MutexLock _lock(&mutex_);
      if (!waiters_.empty()) {
        std::pair<Scheduler *, Fiber::ptr> next = waiters_.front();
        waiters_.pop_front();
        next.first->schedule(next.second);
      } else {
        ++concurrency_;
      }
    }

    FiberCondition::~FiberCondition() noexcept(false) {
    }

    void FiberCondition::wait() {
      SPAN_ASSERT(Scheduler::getThis());
      {
        absl::MutexLock _lock(&mutex_);
        absl::MutexLock _fiberLock(&fiberMutex_->mutex_);
        SPAN_ASSERT(fiberMutex_->owner_ == Fiber::getThis());
        waiters_.push_back(std::make_pair(Scheduler::getThis(), Fiber::getThis()));
        fiberMutex_->unlockNoLock();
      }
      Scheduler::yieldTo();
    }

    void FiberCondition::signal() {
      std::pair<Scheduler *, Fiber::ptr> next;
      {
        absl::MutexLock _lock(&mutex_);
        if (waiters_.empty()) {
          return;
        }
        next = waiters_.front();
        waiters_.pop_front();
      }
      absl::MutexLock _lock(&fiberMutex_->mutex_);
      SPAN_ASSERT(fiberMutex_->owner_ != next.second);
      if (!fiberMutex_->owner_) {
        fiberMutex_->owner_ = next.second;
        next.first->schedule(next.second);
      } else {
        fiberMutex_->waiters_.push_back(next);
      }
    }

    void FiberCondition::broadcast() {
      absl::MutexLock _lock(&mutex_);
      if (waiters_.empty()) {
        return;
      }
      absl::MutexLock _lockTwo(&fiberMutex_->mutex_);

      std::list<std::pair<Scheduler *, Fiber::ptr>>::iterator it;
      for (it = waiters_.begin(); it != waiters_.end(); ++it) {
        std::pair<Scheduler *, Fiber::ptr> next = *it;
        SPAN_ASSERT(fiberMutex_->owner_ != next.second);
        if (!fiberMutex_->owner_) {
          fiberMutex_->owner_ = next.second;
          next.first->schedule(next.second);
        } else {
          fiberMutex_->waiters_.push_back(next);
        }
      }
      waiters_.clear();
    }

    FiberEvent::~FiberEvent() noexcept(false) {
    }

    void FiberEvent::wait() {
      {
        absl::MutexLock _lock(&mutex_);
        if (signalled_) {
          if (autoReset_) {
            signalled_ = false;
          }
          return;
        }
        waiters_.push_back(std::make_pair(Scheduler::getThis(), Fiber::getThis()));
      }
      Scheduler::yieldTo();
    }

    void FiberEvent::set() {
      absl::MutexLock _lock(&mutex_);

      if (!autoReset_) {
        signalled_ = true;

        std::list<std::pair<Scheduler *, Fiber::ptr>>::iterator it;
        for (it = waiters_.begin(); it != waiters_.end(); ++it) {
          it->first->schedule(it->second);
        }
        waiters_.clear();
        return;
      }

      if (waiters_.empty()) {
        signalled_ = true;
        return;
      }

      waiters_.front().first->schedule(waiters_.front().second);
      waiters_.pop_front();
    }

    void FiberEvent::reset() {
      absl::MutexLock _lock(&mutex_);
      signalled_ = false;
    }
  }  // namespace fibers
}  // namespace span
