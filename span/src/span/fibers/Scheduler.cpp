#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

#include "span/fibers/Scheduler.hh"
#include "span/exceptions/Assert.hh"
#include "span/fibers/Fiber.hh"

#include "glog/logging.h"

namespace span {
  namespace fibers {
    thread_local Scheduler* Scheduler::threadLocalScheduler = nullptr;
    thread_local Fiber* Scheduler::threadLocalFiber = nullptr;

    Scheduler::Scheduler(size_t threads, bool useCaller, size_t pBatchSize)
      : activeThreadCount(0), idleThreadCount(0), stopping(true), autoStop(false), batchSize(pBatchSize) {
      SPAN_ASSERT(threads >= 1);

      if (useCaller) {
        --threads;
        SPAN_ASSERT(getThis() == NULL);
        threadLocalScheduler = this;
        rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this)));
        threadLocalScheduler = this;
        threadLocalFiber = rootFiber.get();
        rootThread = std::this_thread::get_id();
      }

      threadCount = threads;
    }

    Scheduler::~Scheduler() noexcept(false) {
      SPAN_ASSERT(stopping);
      if (getThis() == this) {
        threadLocalScheduler = NULL;
      }
    }

    Scheduler * Scheduler::getThis() {
      return threadLocalScheduler;
    }

    void Scheduler::start() {
      LOG(INFO) << this << " starting " << threadCount << " threads";
      absl::MutexLock lock(&mutex);
      if (!stopping) {
        return;
      }
      // TODO(kfc): There may be a race condition here if one thread calls stop(),
      // and another thread calls start() before the worker threads for this
      // scheduler actually exit; they may resurrect themselves, and the stopping
      // thread would block waiting for the thread to exit

      stopping = false;
      SPAN_ASSERT(threads.empty());
      threads.resize(threadCount);
      for (size_t i = 0; i < threadCount; ++i) {
        threads[i] = std::shared_ptr<std::thread>(new std::thread(
          std::bind(&Scheduler::run, this)));
      }
    }

    bool Scheduler::hasWorkToDo() {
      absl::MutexLock lock(&mutex);
      return !fibers.empty();
    }

    void Scheduler::stop() {
      // Check if we're already stopped.
      if (rootFiber && threadCount == 0 &&
        (rootFiber->state() == Fiber::TERM || rootFiber->state() == Fiber::INIT)) {
        LOG(INFO) << this << " stopped.";
        stopping = true;

        // A Derived Class may inhibit stopping while it has things
        // to do in it's idle loop, so we can't break early.
        if (Stopping()) {
          return;
        }
      }

      bool exitOnThisFiber = false;
      if (rootThread != std::thread::id()) {
        //  A Thread Hijacking Scheduler must be stopped from within itself.
        // To return control to the original thread.
        SPAN_ASSERT(Scheduler::getThis() == this);
        if (Fiber::getThis() == callingFiber) {
          exitOnThisFiber = true;
          LOG(INFO) << this << " switching to root thread to stop.";
          switchTo(rootThread);
        }
        if (!callingFiber) {
          exitOnThisFiber = true;
        }
      } else {
        // A spawned-threads only scheduler cannot be stopped from within
        // itself... who would get control?
        SPAN_ASSERT(Scheduler::getThis() != this);
      }
      stopping = true;

      for (size_t i = 0; i < threadCount; ++i) {
        tickle();
      }
      if (rootFiber && (threadCount != 0u || Scheduler::getThis() != this)) {
        tickle();
      }
      // Wait for all work to stop on this thread.
      if (exitOnThisFiber) {
        while (!Stopping()) {
          // Give this threads run fiber a chance to kill itself off.
          LOG(INFO) << this << " yielding to thread to stop";
          yieldTo(true);
        }
      }
      // Wait for other threads to stop.
      if (exitOnThisFiber || Scheduler::getThis() != this) {
        LOG(INFO) << this << " waiting for other threads to stop.";
        std::vector<std::shared_ptr<std::thread>> theThreads;
        {
          absl::MutexLock lock(&mutex);
          theThreads.swap(threads);
        }
        for (std::shared_ptr<std::thread> tempThread : theThreads) {
          tempThread->join();
        }
      }

      LOG(INFO) << this << " stopped.";
    }

    bool Scheduler::Stopping() {
      absl::MutexLock _lock(&mutex);
      return stopping && fibers.empty() && activeThreadCount == 0;
    }

    void Scheduler::switchTo(std::thread::id thread) {
      SPAN_ASSERT(Scheduler::getThis() != NULL);
      if (Scheduler::getThis() == this) {
        if (thread == std::thread::id() || thread == std::this_thread::get_id()) {
          return;
        }
      }
      LOG(INFO) << this << " switching to thread " << thread;
      schedule(Fiber::getThis(), thread);
      Scheduler::yieldTo();
    }

    void Scheduler::yieldTo() {
      Scheduler *self = Scheduler::getThis();
      SPAN_ASSERT(self);
      LOG(INFO) << self << " yielding to scheduler";
      SPAN_ASSERT(threadLocalFiber);
      if (self->rootThread == std::this_thread::get_id() &&
        (threadLocalFiber->state() == Fiber::INIT || threadLocalFiber->state() == Fiber::TERM)) {
        self->callingFiber = Fiber::getThis();
        self->yieldTo(true);
      } else {
        self->yieldTo(false);
      }
    }

    void Scheduler::yield() {
      SPAN_ASSERT(Scheduler::getThis());
      Scheduler::getThis()->schedule(Fiber::getThis());
      yieldTo();
    }

    void Scheduler::dispatch() {
      LOG(INFO) << this << " dispatching";
      SPAN_ASSERT(rootThread == std::this_thread::get_id() && threadCount == 0);
      stopping = true;
      autoStop = true;
      yieldTo();
      autoStop = false;
    }

    void Scheduler::ThreadCount(size_t theNewThreadCount) {
      SPAN_ASSERT(theNewThreadCount >= 1);
      if (rootFiber) {
        --theNewThreadCount;
      }
      absl::MutexLock _lock(&mutex);
      if (theNewThreadCount == threadCount) {
        return;
      } else {
        threads.resize(theNewThreadCount);
        for (size_t i = 0; i < theNewThreadCount; ++i) {
          threads[i] = std::shared_ptr<std::thread>(new std::thread(
            std::bind(&Scheduler::run, this)));
        }
      }
      threadCount = theNewThreadCount;
    }

    void Scheduler::yieldTo(bool yieldToCallerOnTerminate) {
      SPAN_ASSERT(threadLocalFiber);
      SPAN_ASSERT(Scheduler::getThis() == this);
      if (yieldToCallerOnTerminate) {
        SPAN_ASSERT(rootThread == std::this_thread::get_id());
      }
      if (threadLocalFiber->state() != Fiber::HODL) {
        stopping = autoStop || stopping;
        /// XXX: Is threadLocalFiber the hijacked thread?
        threadLocalFiber->reset(std::bind(&Scheduler::run, this));
      }
      threadLocalFiber->yieldTo(yieldToCallerOnTerminate);
    }

    void Scheduler::run() {
      setThis();
      if (std::this_thread::get_id() != rootThread) {
        // Running in our own thread.
        threadLocalFiber = Fiber::getThis().get();
      } else {
        // Hijacked a Thread.
        SPAN_ASSERT(threadLocalFiber == Fiber::getThis().get());
      }
      Fiber::ptr idleFiber(new Fiber(std::bind(&Scheduler::idle, this)));
      LOG(INFO) << this << " starting thread with idle fiber " << idleFiber;
      Fiber::ptr dgFiber;
      // Use a vector for an O(1) .size()
      std::vector<FiberAndThread> batch;
      batch.reserve(batchSize);
      bool isActive = false;
      while (true) {
        SPAN_ASSERT(batch.empty());
        bool dontIdle = false;
        bool tickleMe = false;
        {
          absl::MutexLock _lock(&mutex);
          // Kill ourselves off if needeed.
          if (threads.size() > threadCount && std::this_thread::get_id() != rootThread) {
            // Accounting
            if (isActive) {
              --activeThreadCount;
            }
            // Kill off the idle fiber.
            try {
              throw std::logic_error("Killing off the fiber because too many threads.");
            } catch (...) {
              idleFiber->inject(std::current_exception());
            }
            // Detach our thread.
            for (std::vector<std::shared_ptr<std::thread>>::iterator it = threads.begin();
              it != threads.end(); ++it) {
                if ((*it)->get_id() == std::this_thread::get_id()) {
                  threads.erase(it);
                  if (threads.size() > threadCount) {
                    tickle();
                  }
                  return;
                }
              }

            /// Impossible to reach.
            SPAN_ASSERT(false);
          }

          std::vector<FiberAndThread>::iterator it(fibers.begin());
          while (it != fibers.end()) {
            // If we've met our batch size, and we're not checking to see
            // if we need to tickle another thread, then break
            if ((tickleMe || activeThreadCount == ThreadCount()) && batch.size() == batchSize) {
              break;
            }
            if (it->thread != std::thread::id() && it->thread != std::this_thread::get_id()) {
              LOG(INFO) << this << " scheduled item skipping for this thread: " << it->thread;

              // Wake up another thread to hopefully service this.
              tickleMe = true;
              dontIdle = true;
              ++it;
              continue;
            }
            SPAN_ASSERT(it->fiber || it->dg);
            // This fiber is still executing; probably just some race race condition that it
            // needs to yield on one thread before running on another thread
            if (it->fiber && it->fiber->state() == Fiber::EXEC) {
              LOG(INFO) << this << " skipping executing fiber: " << it->fiber;
              ++it;
              dontIdle = true;
              continue;
            }
            // We were just checking if there is more work; there is so set the flag
            // and don't actually take this piece of work.
            if (batch.size() == batchSize) {
              tickleMe = true;
              break;
            }
            batch.push_back(*it);
            it = fibers.erase(it);
            if (!isActive) {
              activeThreadCount++;
              isActive = true;
            }
          }

          if (batch.empty() && isActive) {
            --activeThreadCount;
            isActive = false;
          }
        }

        if (tickleMe) {
          tickle();
        }

        LOG(INFO) << this << " got " << batch.size() << " fibers/dgs to process (max: "
          << batchSize << ", active: " << isActive<< ")";
        SPAN_ASSERT(isActive == !batch.empty());

        if (batch.empty()) {
          if (dontIdle) {
            continue;
          }

          if (idleFiber->state() == Fiber::TERM) {
            LOG(INFO) << this << " idle fiber terminated.";
            if (std::this_thread::get_id() == rootThread) {
              callingFiber.reset();
            }
            // Unblock the next thread.
            if (ThreadCount() > 1) {
              tickle();
            }
            return;
          }

          LOG(INFO) << this << " idling.";
          idleThreadCount++;
          idleFiber->call();
          idleThreadCount--;
          continue;
        }

        while (!batch.empty()) {
          FiberAndThread& ft = batch.back();
          Fiber::ptr f = ft.fiber;
          std::function<void()> dg = ft.dg;
          batch.pop_back();

          try {
            if (f && f->state() != Fiber::TERM) {
              LOG(INFO) << this << " running: " << f;
              f->yieldTo();
            } else if (dg) {
              if (dgFiber) {
                dgFiber->reset(dg);
              } else {
                dgFiber.reset(new Fiber(dg));
              }
              LOG(INFO) << this << " running.";
              dg = NULL;
              dgFiber->yieldTo();
              if (dgFiber->state() != Fiber::TERM) {
                dgFiber.reset();
              } else {
                dgFiber->reset(NULL);
              }
            }
          } catch(...) {
            {
              absl::MutexLock _lock(&mutex);
              copy(batch.begin(), batch.end(), back_inserter(fibers));
              batch.clear();
              // decrease activeThreadCount as this is an exception
              isActive = false;
              --activeThreadCount;
            }
            throw;
          }
        }
      }
    }

    SchedulerSwitcher::SchedulerSwitcher(Scheduler *target) {
      caller = Scheduler::getThis();
      if (target) {
        target->switchTo();
      }
    }

    SchedulerSwitcher::~SchedulerSwitcher() {
      if (caller) {
        caller->switchTo();
      }
    }
  }  // namespace fibers
}  // namespace span
