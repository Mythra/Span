#include <atomic>
#include "gtest/gtest.h"

#include "span/fibers/Fiber.hh"
#include "span/fibers/WorkerPool.hh"

using span::fibers::WorkerPool;
using span::fibers::Fiber;
using span::fibers::Scheduler;

namespace {
  static void doNothing() {}

  TEST(SchedulerTests, stopWorksMultipleTimes) {
    WorkerPool pool;
    pool.stop();
    pool.stop();
  }

  TEST(SchedulerTests, stopWorksMultipleTimesHybrid) {
    WorkerPool pool(2);
    pool.stop();
    pool.stop();
  }

  TEST(SchedulerTests, stopWorksMultipleTimesSpawn) {
    WorkerPool pool(1, false);
    pool.stop();
    pool.stop();
  }

  TEST(SchedulerTests, startWorksMultipleTimes) {
    WorkerPool pool;
    pool.start();
    pool.start();
  }

  TEST(SchedulerTests, startWorksMultipleTimesHybrid) {
    WorkerPool pool(2);
    pool.start();
    pool.start();
  }

  TEST(SchedulerTests, startWorksMultipleTimesSpawn) {
    WorkerPool pool(1, false);
    pool.start();
    pool.start();
  }

  TEST(SchedulerTests, stopScheduledHijack) {
    WorkerPool pool;
    pool.schedule(std::bind(&Scheduler::stop, &pool));
    pool.dispatch();
  }

  TEST(SchedulerTests, stopScheduledHybrid) {
    WorkerPool pool;
    pool.schedule(std::bind(&Scheduler::stop, &pool));
    pool.yieldTo();
  }

  TEST(SchedulerTests, hijackBasic) {
    Fiber::ptr doNothingFiber(new Fiber(&doNothing));
    WorkerPool pool;
    EXPECT_TRUE(Scheduler::getThis() == &pool);
    pool.schedule(doNothingFiber);
    EXPECT_TRUE(doNothingFiber->state() == Fiber::INIT);
    pool.dispatch();
    EXPECT_TRUE(doNothingFiber->state() == Fiber::TERM);
  }

  TEST(SchedulerTests, hijackMultipleDispatch) {
    Fiber::ptr doNothingFiber(new Fiber(&doNothing));
    WorkerPool pool;
    EXPECT_TRUE(Scheduler::getThis() == &pool);
    pool.schedule(doNothingFiber);
    EXPECT_TRUE(doNothingFiber->state() == Fiber::INIT);
    pool.dispatch();
    EXPECT_TRUE(doNothingFiber->state() == Fiber::TERM);
    doNothingFiber->reset(&doNothing);
    pool.schedule(doNothingFiber);
    EXPECT_TRUE(doNothingFiber->state() == Fiber::INIT);
    pool.dispatch();
    EXPECT_TRUE(doNothingFiber->state() == Fiber::TERM);
  }

  TEST(SchedulerTests, hijackStopOnScheduled) {
    Fiber::ptr doNothingFiber(new Fiber(&doNothing));
    WorkerPool pool;
    EXPECT_TRUE(Scheduler::getThis() == &pool);
    pool.schedule(doNothingFiber);
    EXPECT_TRUE(doNothingFiber->state() == Fiber::INIT);
    pool.stop();
    EXPECT_TRUE(doNothingFiber->state() == Fiber::TERM);
  }
}  // namespace
