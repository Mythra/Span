#ifndef SPAN_SRC_SPAN_IO_IOMANAGEREPOLL_HH_
#define SPAN_SRC_SPAN_IO_IOMANAGEREPOLL_HH_

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "span/Common.hh"
#include "span/Timer.hh"
#include "span/fibers/Scheduler.hh"

#include "absl/synchronization/mutex.h"

#if PLATFORM == PLATFORM_UNIX
#if UNIX_FLAVOUR != UNIX_FLAVOUR_BSD && UNIX_FLAVOUR != UNIX_FLAVOUR_OSX

namespace span {
  namespace fibers {
    class Fiber;
  }

  namespace io {
    class IOManager : public span::fibers::Scheduler, public span::TimerManager {
    public:
      enum Event {
        NONE  = 0x0000,
        READ  = 0x0001,
        WRITE = 0x0004,
        CLOSE = 0x2000
      };

      /**
       * @param autoStart - Whether or not to call the start() automatically in the constructor.
       *
       * NOTE: @p autoStart provides a more friendly behavior for dervied classes.
       */
      explicit IOManager(size_t threads = 1, bool useCaller = true, bool autoStart = true);
      ~IOManager() noexcept(false);

      bool stopping();

      void registerEvent(int fd, Event events, std::function<void()> dg = NULL);
      /**
       * Unregisters an event, returning true if it was successfully unregistered.
       *
       * NOTE: This does not fire the event.
       */
      bool unregisterEvent(int fd, Event events);
      /**
       * NOTE: This function will cause the event to fire.
       */
      bool cancelEvent(int fd, Event events);

    protected:
      bool stopping(uint64 *nextTimeout);
      void idle();
      void tickle();

      void onTimerInsertedAtFront() {
        tickle();
      }

    private:
      struct AsyncState {
        AsyncState();
        ~AsyncState() noexcept(false);
        AsyncState(const AsyncState& rhs) = delete;

        struct EventContext {
          EventContext() : scheduler(NULL) {}
          span::fibers::Scheduler *scheduler;
          std::shared_ptr<span::fibers::Fiber> fiber;
          std::function<void()> dg;
        };

        EventContext &contextForEvent(Event event);
        bool triggerEvent(Event event, std::atomic<size_t> *pendingEventCount);
        void resetContext(EventContext &);

        int fd;
        EventContext in, out, close;
        Event events;
        absl::Mutex mutex;

      private:
        void asyncResetContext(EventContext &);
      };

      int epfd;
      int tickleFds[2];
      std::atomic<size_t> pendingEventCount;
      absl::Mutex mutex;
      std::vector<AsyncState*> pendingEvents;
    };
  }  // namespace io
}  // namespace span

#endif
#endif

#endif  // SPAN_SRC_SPAN_IO_IOMANAGEREPOLL_HH_
