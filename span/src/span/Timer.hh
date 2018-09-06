#ifndef SPAN_SRC_SPAN_TIMER_HH_
#define SPAN_SRC_SPAN_TIMER_HH_

#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "span/Common.hh"

namespace span {
  uint64 muldiv64(uint64 a, uint32 b, uint64 c);

  class TimerManager;

  class Timer : public std::enable_shared_from_this<Timer> {
    friend class TimerManager;

  public:
    typedef std::shared_ptr<Timer> ptr;

    bool cancel();
    bool refresh();
    bool reset(uint64 us, bool fromNow);

  private:
    Timer(uint64 us, std::function<void()> dg, bool recurring, TimerManager *manager);
    // Constructor for Dummy object.
    explicit Timer(uint64 next);
    Timer(const Timer& rhs) = delete;

    bool recurring;
    uint64 next;
    uint64 us;
    std::function<void()> dg;
    TimerManager *manager;

    struct Comparator {
      bool operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const;
    };
  };

  class TimerManager {
    friend class Timer;

  public:
    TimerManager();
    TimerManager(const TimerManager& rhs) = delete;
    virtual ~TimerManager() noexcept(false);

    virtual Timer::ptr registerTimer(uint64 us, std::function<void()> dg, bool recurring = false);

    template<class Rep, class Period>
    Timer::ptr registerTimer(std::chrono::duration<Rep, Period> duration, std::function<void()> dg,
      bool recurring = false) {
      auto rescaled = std::chrono::duration_cast<std::chrono::microseconds>(duration);

      return registerTimer(rescaled.count(), dg, recurring);
    }

    Timer::ptr registerConditionTimer(uint64 us, std::function<void()> dg, std::weak_ptr<void> weakCond,
      bool recurring = false);

    uint64 nextTimer();
    void executeTimers();

    static uint64 now();

    // NOTE: This affects all clock instances cause static.
    static void setClock(std::function<uint64()> dg = NULL);

  protected:
    virtual void onTimerInsertedAtFront() {}
    std::vector<std::function<void()>> processTimers();

  private:
    bool detectClockRollover(uint64 nowUs);
    static std::function<uint64()> clockDg;
    std::set<Timer::ptr, Timer::Comparator> timers;
    absl::Mutex mutex;
    bool tickled;
    uint64 previousTime;
  };
}  // namespace span

#endif  // SPAN_SRC_SPAN_TIMER_HH_
