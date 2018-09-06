#include "span/Sleep.hh"

#include "span/Common.hh"
#include "span/exceptions/Assert.hh"
#include "span/fibers/Fiber.hh"
#include "span/fibers/Scheduler.hh"
#include "span/Timer.hh"

namespace span {
  static void scheduleMe(fibers::Scheduler *scheduler, fibers::Fiber::ptr fiber) {
    scheduler->schedule(fiber);
  }

  void sleep(TimerManager *timerManager, uint64 us) {
    SPAN_ASSERT(fibers::Scheduler::getThis());
    timerManager->registerTimer(us, std::bind(&scheduleMe, fibers::Scheduler::getThis(), fibers::Fiber::getThis()));
    fibers::Scheduler::yieldTo();
  }

  void sleep(uint64 us) {
#if PLATFORM == PLATFORM_WIN32
    Sleep(static_cast<DWORD>(us / 1000));
#else
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000;
    while (true) {
      if (nanosleep(&ts, &ts) == -1) {
        if (errno == EINTR) {
          continue;
        }
        throw std::runtime_error("nanosleep");
      }
      break;
    }
#endif
  }
}  // namespace span
