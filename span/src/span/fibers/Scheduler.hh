#ifndef SPAN_SRC_SPAN_FIBERS_SCHEDULER_HH_
#define SPAN_SRC_SPAN_FIBERS_SCHEDULER_HH_

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "absl/synchronization/mutex.h"

namespace span {
  namespace fibers {
    class Fiber;

    /// Cooperative Fiber Scheduler.
    ///
    /// A Scheduler can be used to schedule Fibers to be executed.
    /// Specifically by implementing an M on N threading model.
    /// A scheduler can either "hijack" the thread it was created on,
    /// or spawn multiple threads of it's own, or a hybrid of the two.
    ///
    /// Hijacking and Schedulers begin processing when either yieldTo(),
    /// or dispatch() is called. The Scheduler itself will stop when
    /// there are no more fiber scheduled, and return from yieldTo() or
    /// dispatch(). Hybrid and spawned Schedulers must be explicitly stopped
    /// via stop(). stop() will only return when all work is done.
    class Scheduler {
    public:
      /// Default Constructor for a Scheduler.
      ///
      /// By Default a Single Thread Hijacking Scheduler is constructed.
      /// If Threads > 1, and useCaller then a hybrid hijack + spawn is constructed.
      /// If !useCaller then a non-hijacking scheduler is constructed.
      ///
      /// If you specify useCaller to true, Scheduler::getThis() must be NULL.
      explicit Scheduler(size_t threads = 1, bool useCaller = true, size_t batchSize = 1);

      /// Destroys the Scheduler implcitly calling Stop().
      virtual ~Scheduler() noexcept(false);

      /// Get the scheduler controlling the current thread.
      static Scheduler* getThis();

      /// Explicitly start the scheduler.
      ///
      /// Derived classes should call start() in their constructor.
      /// It is safe to call start if already started (becomes a no-op).
      void start();

      /// Explicitly stop the scheduler.
      ///
      /// This must be called for hybrid, and spawned schedulers. It is safe
      /// to call stop even if the scheduler is already stopped, or stopping.
      /// it becomes a no-op.
      ///
      /// For Hybrid, or Hijacking Schedulers it must be called from within the
      /// scheduler. For spawned schedulers it can be called from outside. If you
      /// call outside of this context we'll just return immediatly.
      ///
      /// Stop is blocking, and will not return til work is done.
      void stop();

      /// Schedule a fiber to be executed on the Scheduler.
      ///
      /// fd - The Fiber or Functor to be scheduled. If a pointer
      /// is passed in the ownership will transfer to this scheduler.
      template<class FiberOrDg>
      void schedule(FiberOrDg fd, std::thread::id thread = {});

      /// Schedule multiple items to be executed at once.
      template<class InputIterator>
      void schedule(InputIterator begin, InputIterator end);

      /// Change the currently executing fiber to be running on this scheduler.
      ///
      /// This function can be used to change which Scheduler/Thread the
      /// currently executing fiber is running on. This switch is done by
      /// rescheduling the Fiber on this Scheduler, and yielding to the current
      /// scheduler.
      void switchTo(std::thread::id thread = {});

      /// This scheduler will not re-schedule this fiber automatically.
      static void yieldTo();

      /// Yield to the scheduler to allow other fibers to execute on this thread.
      ///
      /// The Scheduler will automatically reschedule this fiber.
      static void yield();

      /// Force hijacking a scheduler to process scheduled work.
      ///
      /// Calls yieldTo(), and yields back to the current executing
      /// fiber when there is no more work to be done.
      void dispatch();

      size_t ThreadCount() const;

      /// Change the number of threads for this Scheduler.
      void ThreadCount(size_t numThreads);

      const std::vector<std::shared_ptr<std::thread>>& Threads() const;

      std::thread::id rootThreadId() const {
        return rootThread;
      }

    protected:
      /// Dervied classes can query stopping() to determine if the scheduler is stopping.
      ///
      /// When this is true the idle fiber should return as fast as possible.
      virtual bool Stopping();

      /// This function called in it's own fiber means the scheduler belives there is no work.
      /// The scheduler is not considered stopped until the idle fiber has finished.
      ///
      /// Implementors should Fiber::yield() when it believes there is work scheduled on this
      /// Scheduler.
      virtual void idle() = 0;

      /// The scheduler wants to force the idle fiber to Fiber::yield(), because
      /// new work has been scheduled.
      virtual void tickle() = 0;

      bool hasWorkToDo();
      virtual bool hasIdleThreads() const;
      /// Determine whether tickle() is needed, to be invoked in schedule().
      virtual bool shouldTickle(bool empty) const;

      /// Set 'this' to TLS so that getThis() can get correct scheduler.
      void setThis();

    private:
      void yieldTo(bool yieldToCallerOnTerminate);
      void run();

      template<class FiberOrDg>
      bool scheduleNoLock(FiberOrDg fd, std::thread::id thread = {});

      Scheduler(const Scheduler& rhs) = delete;

      struct FiberAndThread {
        std::shared_ptr<Fiber> fiber;
        std::function<void()> dg;
        std::thread::id thread;

        FiberAndThread(std::shared_ptr<Fiber> f, std::thread::id th) : fiber(f), thread(th) {
        }

        FiberAndThread(std::shared_ptr<Fiber>* f, std::thread::id th) : thread(th) {
          fiber.swap(*f);
        }

        FiberAndThread(std::function<void()> d, std::thread::id th) : dg(d), thread(th) {
        }

        FiberAndThread(std::function<void()> *d, std::thread::id th) : thread(th) {
          dg.swap(*d);
        }
      };

      static thread_local Scheduler* threadLocalScheduler;
      static thread_local Fiber* threadLocalFiber;

      absl::Mutex mutex;
      std::vector<FiberAndThread> fibers;
      std::thread::id rootThread;
      std::shared_ptr<Fiber> rootFiber;
      std::shared_ptr<Fiber> callingFiber;
      std::vector<std::shared_ptr<std::thread>> threads;
      size_t threadCount, activeThreadCount;
      std::atomic<size_t> idleThreadCount;
      bool stopping;
      bool autoStop;
      size_t batchSize;
    };

    /// Automatic Scheduler Switcher
    ///
    /// Automatically switches to Scheduler::getThis() when going out of scope.
    /// by calling (Scheduler::switchTo()).
    struct SchedulerSwitcher {
    public:
      // Capture Scheduler::getThis(), and optionally calls target->switchTo();
      explicit SchedulerSwitcher(Scheduler *target = NULL);
      // Calls switchTo() on the scheduler captured in the constructor.
      ~SchedulerSwitcher();
      SchedulerSwitcher(const SchedulerSwitcher& rhs) = delete;

    private:
      Scheduler *caller;
    };

    template<class FiberOrDg>
    inline void Scheduler::schedule(FiberOrDg fd, std::thread::id thread) {
      bool tickleMe;
      {
        absl::MutexLock _lock(&mutex);
        tickleMe = scheduleNoLock(fd, thread);
      }
      if (shouldTickle(tickleMe)) {
        tickle();
      }
    }

    template<class InputIterator>
    inline void Scheduler::schedule(InputIterator begin, InputIterator end) {
      bool tickleMe = false;
      {
        absl::MutexLock _lock(&mutex);
        while (begin != end) {
          tickleMe = scheduleNoLock(&*begin) || tickleMe;
          ++begin;
        }
      }
      if (shouldTickle(tickleMe)) {
        tickle();
      }
    }

    inline size_t Scheduler::ThreadCount() const {
      return threadCount + (rootFiber ? 1 : 0);
    }

    inline const std::vector<std::shared_ptr<std::thread>>& Scheduler::Threads() const {
      return threads;
    }

    inline void Scheduler::setThis() {
      threadLocalScheduler = this;
    }

    inline bool Scheduler::hasIdleThreads() const {
      return idleThreadCount != 0;
    }

    inline bool Scheduler::shouldTickle(bool empty) const {
      return empty && Scheduler::getThis() != this;
    }

    template<class FiberOrDg>
    inline bool Scheduler::scheduleNoLock(FiberOrDg fd, std::thread::id thread) {
      bool tickleMe = fibers.empty();
      fibers.push_back(FiberAndThread(fd, thread));
      return tickleMe;
    }
  }  // namespace fibers
}  // namespace span

#endif  // SPAN_SRC_SPAN_FIBERS_SCHEDULER_HH_
