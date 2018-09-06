#include "gtest/gtest.h"

#include "span/Common.hh"
#include "span/Timer.hh"
#include "span/fibers/WorkerPool.hh"

using span::Timer;
using span::TimerManager;
using span::fibers::WorkerPool;

static void singleTimer(int *sequence, int *expected) {
  ++*sequence;
  EXPECT_EQ(*sequence, *expected);
}

namespace {
  TEST(Timer, single) {
    int sequence = 0;
    int expected = 1;
    TimerManager manager;
    EXPECT_EQ(manager.nextTimer(), ~0ull);
    manager.registerTimer(0, std::bind(&singleTimer, &sequence, &expected));
    EXPECT_EQ(manager.nextTimer(), 0u);
    EXPECT_EQ(sequence, 0);
    manager.executeTimers();
    ++sequence;
    EXPECT_EQ(sequence, 2);
    EXPECT_EQ(manager.nextTimer(), ~0ull);
  }

  TEST(Timer, multiple) {
    int sequence = 0;
    TimerManager manager;
    EXPECT_EQ(manager.nextTimer(), ~0ull);
    manager.registerTimer(0, std::bind(&singleTimer, &sequence, &sequence));
    manager.registerTimer(0, std::bind(&singleTimer, &sequence, &sequence));
    EXPECT_EQ(manager.nextTimer(), 0u);
    EXPECT_EQ(sequence, 0);
    manager.executeTimers();
    ++sequence;
    EXPECT_EQ(sequence, 3);
    EXPECT_EQ(manager.nextTimer(), ~0ull);
  }

  TEST(Timer, cancel) {
    int sequence = 0;
    int expected = 1;
    TimerManager manager;
    EXPECT_EQ(manager.nextTimer(), ~0ull);
    Timer::ptr timer = manager.registerTimer(0, std::bind(&singleTimer, &sequence, &expected));
    EXPECT_EQ(manager.nextTimer(), 0u);
    timer->cancel();
    EXPECT_EQ(manager.nextTimer(), ~0ull);
    manager.executeTimers();
    EXPECT_EQ(sequence, 0);
  }

  TEST(Timer, idempotentCancel) {
    int sequence = 0;
    int expected = 1;
    TimerManager manager;
    EXPECT_EQ(manager.nextTimer(), ~0ull);
    Timer::ptr timer = manager.registerTimer(0, std::bind(&singleTimer, &sequence, &expected));
    EXPECT_EQ(manager.nextTimer(), 0u);
    timer->cancel();
    timer->cancel();
    EXPECT_EQ(manager.nextTimer(), ~0ull);
    manager.executeTimers();
    EXPECT_EQ(sequence, 0);
  }

  TEST(Timer, idempotentCancelAfterSuccess) {
    int sequence = 0;
    int expected = 1;
    TimerManager manager;
    EXPECT_EQ(manager.nextTimer(), ~0ull);
    Timer::ptr timer = manager.registerTimer(0, std::bind(&singleTimer, &sequence, &expected));
    EXPECT_EQ(manager.nextTimer(), 0u);
    EXPECT_EQ(sequence, 0);
    manager.executeTimers();
    ++sequence;
    EXPECT_EQ(sequence, 2);
    EXPECT_EQ(manager.nextTimer(), ~0ull);
    timer->cancel();
    timer->cancel();
    EXPECT_EQ(sequence, 2);
    EXPECT_EQ(manager.nextTimer(), ~0ull);
  }

  TEST(Timer, recurring) {
    int sequence = 0;
    int expected;
    TimerManager manager;
    EXPECT_EQ(manager.nextTimer(), ~0ull);
    Timer::ptr timer = manager.registerTimer(0, std::bind(&singleTimer, &sequence, &expected), true);
    EXPECT_EQ(manager.nextTimer(), 0u);
    EXPECT_EQ(sequence, 0);
    expected = 1;
    manager.executeTimers();
    ++sequence;
    EXPECT_EQ(sequence, 2);
    EXPECT_EQ(manager.nextTimer(), 0u);
    expected = 3;
    manager.executeTimers();
    ++sequence;
    EXPECT_EQ(sequence, 4);
    timer->cancel();
    EXPECT_EQ(manager.nextTimer(), ~0ull);
  }

  TEST(Timer, later) {
    int sequence = 0;
    int expected = 1;
    TimerManager manager;
    EXPECT_EQ(manager.nextTimer(), ~0ull);
    Timer::ptr timer = manager.registerTimer(1000 * 1000 * 1000, std::bind(&singleTimer, &sequence, &expected));

    auto lhs = manager.nextTimer();
    auto rhs = 1000 * 1000 * 1000 * 1000u;
    auto variance = 100 * 1000 * 1000u;
    EXPECT_TRUE(!(lhs - variance <= rhs && lhs + variance > rhs));

    EXPECT_EQ(sequence, 0);
    manager.executeTimers();
    ++sequence;
    EXPECT_EQ(sequence, 1);
    timer->cancel();
    EXPECT_EQ(manager.nextTimer(), ~0ull);
  }

  static uint64 fakeClock(uint64 *clock) {
    return *clock;
  }

  TEST(Timer, rollover) {
    // Two minutes before apocalypse.
    static uint64 clock = 0ULL - 120000000;
    TimerManager::setClock(std::bind(&fakeClock, &clock));

    int sequence = 0;
    TimerManager manager;

    // sanity check - test passage of time.
    Timer::ptr timer1 = manager.registerTimer(60000000, std::bind(&singleTimer, &sequence, &sequence));
    EXPECT_EQ(manager.nextTimer(), 60000000ULL);
    clock += 30000000;
    manager.executeTimers();
    EXPECT_EQ(sequence, 0);  // timer hasn't fire yet.
    EXPECT_EQ(manager.nextTimer(), 30000000ULL);  // still 30 seconds out

    // now create more, cause testing more is better?
    Timer::ptr timer2 = manager.registerTimer(15000000, std::bind(&singleTimer, &sequence, &sequence));
    EXPECT_EQ(manager.nextTimer(), 15000000ULL);
    Timer::ptr timer3 = manager.registerTimer(180000000, std::bind(&singleTimer, &sequence, &sequence));
    // nextTimer() will return 0 now cause timer3 appears to bein the past.

    // overflow!
    clock += 120000000;
    manager.executeTimers();
    EXPECT_EQ(sequence, 3);  // all timers fired.
    EXPECT_EQ(manager.nextTimer(), ~0ull);

    TimerManager::setClock();
  }
}  // namespace
