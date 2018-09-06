#include "gtest/gtest.h"

#include "span/fibers/Fiber.hh"
#include "span/fibers/Scheduler.hh"

using span::fibers::Fiber;
using span::fibers::FiberLocalStorage;

namespace {
  static void basic(FiberLocalStorage<int> *fls) {
    EXPECT_EQ(fls->get(), 0);
    fls->set(2);
    EXPECT_EQ(fls->get(), 2);
    Fiber::yield();
    EXPECT_EQ(fls->get(), 2);
    fls->set(4);
    EXPECT_EQ(fls->get(), 4);
    Fiber::yield();
    EXPECT_EQ(fls->get(), 4);
    fls->set(6);
    EXPECT_EQ(fls->get(), 6);
  }

  static void thread(FiberLocalStorage<int> *fls, Fiber::ptr fiber) {
    EXPECT_EQ(fls->get(), 0);
    fls->set(3);
    EXPECT_EQ(fls->get(), 3);
    fiber->call();
    EXPECT_EQ(fls->get(), 3);
    fls->set(5);
    EXPECT_EQ(fls->get(), 5);
  }

  TEST(FiberLocalStorageTests, BasicTest) {
    FiberLocalStorage<int> fls;
    EXPECT_EQ(fls.get(), 0);
    fls = 1;
    EXPECT_EQ(fls.get(), 1);

    Fiber::ptr fiber(new Fiber(std::bind(&basic, &fls)));
    fiber->call();
    EXPECT_EQ(fls.get(), 1);

    std::thread thread1(std::bind(&thread, &fls, fiber));
    thread1.join();
    EXPECT_EQ(fls.get(), 1);
    fiber->call();
    EXPECT_EQ(fls.get(), 1);
  }
}  // namespace
