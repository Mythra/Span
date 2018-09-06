#ifndef SPAN_SRC_SPAN_FIBERS_SEMAPHORE_HH_
#define SPAN_SRC_SPAN_FIBERS_SEMAPHORE_HH_

#include "span/Common.hh"

#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
#include <mach/semaphore.h>
#elif PLATFORM == PLATFORM_WIN32
#include <windows.h>
#elif UNIX_FLAVOUR == UNIX_FLAVOUR_BSD
#else
#include <semaphore.h>
#endif

namespace span {
  namespace fibers {
    class Semaphore {
    public:
      explicit Semaphore(unsigned int count = 0);
      explicit Semaphore(const Semaphore& rhs) = delete;
      ~Semaphore() noexcept(false);

      void wait();

      void notify();

    private:
#if PLATFORM == PLATFORM_WIN32
      HANDLE sema;
#elif PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
      task_t task;
      semaphore_t sema;
#elif UNIX_FLAVOUR == UNIX_FLAVOUR_BSD
      int sema;
#else
      sem_t sema;
#endif
    };
  }  // namespace fibers
}  // namespace span

#endif  // SPAN_SRC_SPAN_FIBERS_SEMAPHORE_HH_
