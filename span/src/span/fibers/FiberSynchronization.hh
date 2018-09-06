#ifndef SPAN_SRC_SPAN_FIBERS_FIBERSYNCHRONIZATION_HH_
#define SPAN_SRC_SPAN_FIBERS_FIBERSYNCHRONIZATION_HH_

#include <list>
#include <memory>
#include <utility>

#include "absl/synchronization/mutex.h"

namespace span {
  namespace fibers {
    class Fiber;
    class Scheduler;

    /**
     *  Wraps an absl::Mutex specifically for fibers.
     *
     * FiberMutex will yield to a scheduler, instead of blocking if the mutex cannot be acquired,
     * and will provide a FIFO mechanics for Fibers. Since normally mutex has no idea who to give
     * it too first.
     */
    struct FiberMutex {
      friend struct FiberCondition;

    public:
      FiberMutex() = default;
      ~FiberMutex() noexcept(false);

      void lock();
      void unlock();

      bool unlockIfNotUnique();

    private:
      void unlockNoLock();
      FiberMutex(const FiberMutex& rhs) = delete;

      absl::Mutex mutex_;
      std::shared_ptr<Fiber> owner_;
      std::list<std::pair<Scheduler *, std::shared_ptr<Fiber>>> waiters_;
    };

    // A semaphore for use by fibers that yields to a scheduler instead of blocking.
    struct FiberSemaphore {
    public:
      explicit FiberSemaphore(size_t initialConcurrency = 0);
      ~FiberSemaphore() noexcept(false);

      void wait();
      void notify();

    private:
      FiberSemaphore(const FiberSemaphore& rhs) = delete;

      absl::Mutex mutex_;
      std::list<std::pair<Scheduler *, std::shared_ptr<Fiber>>> waiters_;
      size_t concurrency_;
    };

    /**
     * Scheduler based conidition variable for fibers.
     *
     * Yield to a scheduler instead of blocking.
     */
    struct FiberCondition {
    public:
      explicit FiberCondition(FiberMutex *mutex) : fiberMutex_(mutex) {}
      ~FiberCondition() noexcept(false);

      void wait();
      void signal();
      void broadcast();

    private:
      FiberCondition(const FiberCondition &rhs) = delete;

      absl::Mutex mutex_;
      FiberMutex *fiberMutex_;
      std::list<std::pair<Scheduler *, std::shared_ptr<Fiber>>> waiters_;
    };

    struct FiberEvent {
    public:
      explicit FiberEvent(bool autoReset = true) : signalled_(false), autoReset_(autoReset) {}
      ~FiberEvent() noexcept(false);

      void wait();
      void set();
      void reset();

    private:
      FiberEvent(const FiberEvent& rhs) = delete;

      absl::Mutex mutex_;
      bool signalled_, autoReset_;
      std::list<std::pair<Scheduler *, std::shared_ptr<Fiber>>> waiters_;
    };
  }  // namespace fibers
}  // namespace span

#endif  // SPAN_SRC_SPAN_FIBERS_FIBERSYNCHRONIZATION_HH_
