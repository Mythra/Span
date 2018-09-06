#include <memory>

#include "gtest/gtest.h"

#include "span/io/IOManager.hh"
#include "span/Timer.hh"

using span::Timer;
using span::io::IOManager;

namespace {
  class EmptyTimeClass {
    public:
      typedef std::shared_ptr<EmptyTimeClass> ptr;
      EmptyTimeClass() : timedOut(false) {}

      void onTimer() {
        timedOut = true;
      }

      bool TimedOut() const { return timedOut; }

    private:
      bool timedOut;
  };

  void testTimerNoExpire(IOManager *manager) {
    EmptyTimeClass::ptr tester(new EmptyTimeClass());
    EXPECT_TRUE(tester.unique());
    Timer::ptr timer = manager->registerTimer(30000000,
      std::bind(&EmptyTimeClass::onTimer, tester));
    EXPECT_FALSE(tester.unique());
    timer->cancel();
    EXPECT_FALSE(tester->TimedOut());
    EXPECT_TRUE(tester.unique());
  }
}  // namespace

static void singleTimer(int *sequence, int *expected) {
  ++*sequence;
  EXPECT_EQ(*sequence, *expected);
}

TEST(IoManagerTests, singleTimerTest) {
  int sequence = 0;
  int expected = 1;
  IOManager manager;
  manager.registerTimer(0, std::bind(&singleTimer, &sequence, &expected));
  EXPECT_EQ(sequence, 0);
  manager.dispatch();
  ++sequence;
  EXPECT_EQ(sequence, 2);
}

TEST(IoManagerTests, laterTimer) {
  int sequence = 0;
  int expected = 1;
  IOManager manager;
  manager.registerTimer(100000, std::bind(&singleTimer, &sequence, &expected));
  EXPECT_EQ(sequence, 0);
  manager.dispatch();
  ++sequence;
  EXPECT_EQ(sequence, 2);
}

TEST(IoManagerTests, timerRefCountNotExpired) {
  IOManager manager;
  manager.schedule(std::bind(testTimerNoExpire, &manager));
  manager.dispatch();
}
