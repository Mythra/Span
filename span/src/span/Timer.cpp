#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include "span/Timer.hh"
#include "span/exceptions/Assert.hh"

#include "glog/logging.h"

#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
#include <mach/mach_time.h>
#elif PLATFORM == PLATFORM_WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <time.h>
#endif

namespace span {

  std::function<uint64()> TimerManager::clockDg;
  constexpr uint64 clockRolloverThreshold = 5000000ULL;

  static void stubOnTimer(std::weak_ptr<void> weakCond, std::function<void()> dg);

  uint64 muldiv64(uint64 a, uint32 b, uint64 c) {
    union {
        uint64 ll;
        struct {
            uint32 low, high;
        } l;
    } u, res;
    uint64 rl, rh;

    u.ll = a;
    rl = static_cast<uint64>(u.l.low) * static_cast<uint64>(b);
    rh = static_cast<uint64>(u.l.high) * static_cast<uint64>(b);
    rh += (rl >> 32);
    res.l.high = static_cast<uint32>((rh / c));
    res.l.low = static_cast<uint32>(((((rh % c) << 32) + (rl & 0xffffffff)) / c));
    return res.ll;
  }

#if PLATFORM == PLATFORM_WIN32
  static uint64 queryFrequency() {
    LARGE_INTEGER frequency;
    BOOL bRet = QueryPerformanceFrequency(&frequency);
    SPAN_ASSERT(bRet);

    return static_cast<uint64>(frequency.QuadPart);
  }

  uint64 globalFrequency = queryFrequency();
#elif PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
  static mach_timebase_info_data_t queryTimebase() {
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);

    return timebase;
  }

  mach_timebase_info_data_t globalTimebase = queryTimebase();
#endif

  uint64 TimerManager::now() {
    if (clockDg) {
      return clockDg();
    }
#if PLATFORM == PLATFORM_WIN32
    LARGE_INTEGER count;
    if (!QueryPerformanceCounter(&count)) {
      throw std::current_exception();
    }
    uint64 countUll = static_cast<uint64>(count.QuadPart);

    if (globalFrequency == 0) {
      globalFrequency = queryFrequency();
    }

    return muldiv64(countUll, 1000000, globalFrequency);
#elif PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
    uint64 absoluteTime = mach_absolute_time();
    if (globalTimebase.denom == 0) {
      globalTimebase = queryTimebase();
    }

    return muldiv64(absoluteTime, globalTimebase.numer, static_cast<uint64>(globalTimebase.denom * 1000));
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
      throw std::current_exception();
    }

    return ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
#endif
  }

  Timer::Timer(uint64 us, std::function<void()> dg, bool recurring, TimerManager *manager) : recurring(recurring),
    us(us), dg(dg), manager(manager) {
    SPAN_ASSERT(dg);
    next = TimerManager::now() + us;
  }

  Timer::Timer(uint64 next) : next(next) {
  }

  bool Timer::cancel() {
    LOG(INFO) << this << " cancel";
    absl::MutexLock lock(&manager->mutex);
    if (dg) {
      dg = NULL;
      std::set<Timer::ptr, Timer::Comparator>::iterator it = manager->timers.find(shared_from_this());
      SPAN_ASSERT(it != manager->timers.end());
      manager->timers.erase(it);
      return true;
    }
    return false;
  }

  bool Timer::refresh() {
    {
      absl::MutexLock lock(&manager->mutex);
      if (!dg) {
        return false;
      }
      std::set<Timer::ptr, Timer::Comparator>::iterator it = manager->timers.find(shared_from_this());
      SPAN_ASSERT(it != manager->timers.end());
      manager->timers.erase(it);
      next = TimerManager::now() + us;
      manager->timers.insert(shared_from_this());
    }
    LOG(INFO) << this << " refresh";
    return true;
  }

  bool Timer::reset(uint64 pus, bool fromNow) {
    bool atFront;
    {
      absl::MutexLock lock(&manager->mutex);
      if (!dg) {
        return false;
      }

      if (pus == us && !fromNow) {
        return true;
      }

      std::set<Timer::ptr, Timer::Comparator>::iterator it = manager->timers.find(shared_from_this());
      SPAN_ASSERT(it != manager->timers.end());
      manager->timers.erase(it);
      uint64 start;

      if (fromNow) {
        start = TimerManager::now();
      } else {
        start = next - us;
      }
      us = pus;
      next = start + us;

      it = manager->timers.insert(shared_from_this()).first;
      atFront = (it == manager->timers.begin()) && !manager->tickled;
      if (atFront) {
        manager->tickled = true;
      }
    }
    LOG(INFO) << this << " reset to " << us;
    if (atFront) {
      manager->onTimerInsertedAtFront();
    }
    return true;
  }

  TimerManager::TimerManager() : tickled(false), previousTime(0ull) {
  }

  TimerManager::~TimerManager() noexcept(false) {
  }

  Timer::ptr TimerManager::registerTimer(uint64 us, std::function<void()> dg, bool recurring) {
    SPAN_ASSERT(dg);
    Timer::ptr result(new Timer(us, dg, recurring, this));
    bool atFront;
    {
      absl::MutexLock lock(&mutex);
      std::set<Timer::ptr, Timer::Comparator>::iterator it = timers.insert(result).first;

      atFront = (it == timers.begin()) && !tickled;
      if (atFront) {
        tickled = true;
      }
    }
    LOG(INFO) << result.get() << " registerTimer(" << us << ", " << recurring << "): " << atFront;
    if (atFront) {
      onTimerInsertedAtFront();
    }
    return result;
  }

  Timer::ptr TimerManager::registerConditionTimer(uint64 pus, std::function<void()> dg,
    std::weak_ptr<void> weakCond, bool recurring) {
    return registerTimer(pus, std::bind(stubOnTimer, weakCond, dg), recurring);
  }

  static void stubOnTimer(std::weak_ptr<void> weakCond, std::function<void()> dg) {
    std::shared_ptr<void> tmp = weakCond.lock();
    if (tmp) {
      dg();
    } else {
      LOG(INFO) << " Conditionally skip in stubOnTimer!";
    }
  }

  uint64 TimerManager::nextTimer() {
    absl::MutexLock lock(&mutex);
    tickled = false;
    if (timers.empty()) {
      LOG(INFO) << this << " nextTimer(): ~0ull";
      return ~0ull;
    }
    const Timer::ptr &next = *timers.begin();
    uint64 nowUs = now();
    uint64 result;
    if (nowUs >= next->next) {
      result = 0;
    } else {
      result = next->next - nowUs;
    }
    LOG(INFO) << this << " nextTimer(): " << result;
    return result;
  }

  bool TimerManager::detectClockRollover(uint64 nowUs) {
    // If the time jumps backward, expire timers (rather than have them expire in distant future or not at all).
    // We check this way because now() will not roll from 0xffff... to zero since the underlying hardware counter
    // doesn't count microseconds. Use a threshold value so we don't overreact to minor clock jitter.
    bool rollover = false;
    if (nowUs < previousTime && nowUs < previousTime - clockRolloverThreshold) {
      LOG(INFO) << this << " clock has rolled back from " << previousTime << " to " << nowUs << " expiring all timers";
      rollover = true;
    }
    previousTime = nowUs;
    return rollover;
  }

  std::vector<std::function<void()>> TimerManager::processTimers() {
    std::vector<Timer::ptr> expired;
    std::vector<std::function<void()>> result;
    uint64 nowUs = now();
    {
      absl::MutexLock lock(&mutex);
      if (timers.empty()) {
        return result;
      }
      bool rollover = detectClockRollover(nowUs);
      if (!rollover && (*timers.begin())->next > nowUs) {
        return result;
      }
      Timer nowTimer(nowUs);
      Timer::ptr nowTimerPtr(&nowTimer, &nop<Timer *>);

      // Find all expired timers
      std::set<Timer::ptr, Timer::Comparator>::iterator it = rollover ? timers.end() : timers.lower_bound(nowTimerPtr);
      while (it != timers.end() && (*it)->next == nowUs) {
        ++it;
      }

      // Copy to expired, remove from timers.
      expired.insert(expired.begin(), timers.begin(), it);
      timers.erase(timers.begin(), it);
      result.reserve(expired.size());

      // Look at expired timers and re-register recurring timers (while under the same lock)
      for (std::vector<Timer::ptr>::iterator it2(expired.begin()); it2 != expired.end(); ++it2) {
        Timer::ptr &timer = *it2;
        SPAN_ASSERT(timer->dg);
        result.push_back(timer->dg);
        if (timer->recurring) {
          LOG(INFO) << timer << " expired and refreshed";
          timer->next = nowUs + timer->us;
          timers.insert(timer);
        } else {
          LOG(INFO) << timer << " expired";
          timer->dg = NULL;
        }
      }
    }
    return result;
  }

  void TimerManager::executeTimers() {
    std::vector<std::function<void()>> expired = processTimers();
    // Run the callbacks for each expired timer (not under a lock)
    for (std::vector<std::function<void()>>::iterator it(expired.begin()); it != expired.end(); ++it) {
      (*it)();
    }
  }

  void TimerManager::setClock(std::function<uint64()> dg) {
    clockDg = dg;
  }

  bool Timer::Comparator::operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const {
    // Order NULL Before Everything else.
    if (!lhs && !rhs) {
      return false;
    }
    if (!lhs) {
      return true;
    }
    if (!rhs) {
      return false;
    }
    // Order primarily on next
    if (lhs->next < rhs->next) {
      return true;
    }
    if (rhs->next < lhs->next) {
      return false;
    }
    // Order by raw pointer for equivalent timeout values
    return lhs.get() < rhs.get();
  }
}  // namespace span
