#ifndef SPAN_SRC_SPAN_SLEEP_HH_
#define SPAN_SRC_SPAN_SLEEP_HH_

#include <chrono>

#include "span/Common.hh"

namespace span {
  class TimerManager;

  /**
   *  NOTE: this is a normal sleep, and will block current thread.
   *
   * @param us
   *  Microseconds to sleep.
   */
  void sleep(uint64 us);

  template<class Rep, class Period>
  inline void sleep(std::chrono::duration<Rep, Period> duration) {
    auto rescaled = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    sleep(rescaled.count());
  }

  /**
   *  Suspend execution of current Fiber.
   *
   * NOTE: This sleep is fiber aware.
   */
  void sleep(TimerManager *timerManager, uint64 us);

  template<class Rep, class Period>
  inline void sleep(TimerManager *timerManager, std::chrono::duration<Rep, Period> duration) {
    auto rescaled = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    sleep(timerManager, rescaled.count());
  }
}  // namespace span

#endif  // SPAN_SRC_SPAN_SLEEP_HH_
