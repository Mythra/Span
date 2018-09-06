#ifndef SPAN_SRC_SPAN_IO_IOMANAGERKQUEUE_HH_
#define SPAN_SRC_SPAN_IO_IOMANAGERKQUEUE_HH_

#include "span/Common.hh"
#include "span/Timer.hh"
#include "span/fibers/Scheduler.hh"

#if UNIX_FLAVOUR == UNIX_FLAVOUR_BSD

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

#include <functional>
#include <map>
#include <memory>
#include <utility>

#include "absl/synchronization/mutex.h"

namespace span {
  namespace io {
    class IOManager : public span::fibers::Scheduler, public span::TimerManager {
    public:
      enum Event {
        READ,
        WRITE,
        CLOSE
      };

      explicit IOManager(size_t threads = 1, bool useCaller = true, bool autoStart = true);
      ~IOManager();

      bool stopping();

      void registerEvent(int fd, Event events, std::function<void()> dg = NULL);
      void cancelEvent(int fd, Event events);
      void unregisterEvent(int fd, Event events);

    protected:
      bool stopping(uint64 *nextTimeout);
      void idle();
      void tickle();

      void onTimerInsertedAtFront() {
        tickle();
      }

    private:
      struct AsyncEvent {
        struct kevent event;

        Scheduler *scheduler, *schedulerClose;
        std::shared_ptr<span::fibers::Fiber> fiber, fiberClose;
        std::function<void()> dg, dgClose;
      };

      int kqfd;
      int tickleFds[2];
      std::map<std::pair<int, Event>, AsyncEvent> pendingEvents;
      absl::Mutex mutex;
    };
  }  // namespace io
}  // namespace span

#endif

#endif  // SPAN_SRC_SPAN_IO_IOMANAGERKQUEUE_HH_
