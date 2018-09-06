#ifndef SPAN_SRC_SPAN_FIBERS_BASE_UNIXFIBERBASE_HH_
#define SPAN_SRC_SPAN_FIBERS_BASE_UNIXFIBERBASE_HH_

#include <setjmp.h>
#ifdef HAVE_VALGRIND
#include <valgrind/valgrind.h>
#endif

#include "span/Common.hh"

#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
#define _XOPEN_SOURCE
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

namespace span {
  namespace fibers {
    namespace base {
      class FiberBase {
      protected:
        FiberBase();
        explicit FiberBase(uint32 stack_size);
        ~FiberBase();

        virtual void entrypoint() = 0;

        void reset();
        void switchContext(class FiberBase *to);

        void* stackId() {
          return &mCtx;
        }

        void* stackPtr() {
          return mStack;
        }

      private:
        FiberBase(const FiberBase& rhs) = delete;
        static void trampoline(void* ptr);

        bool mInit;
#ifdef HAVE_VALGRIND
        int mValgrindStackId;
#endif
        void* mStack;
        uint32 stackSize;

        union {
          ucontext_t mCtx;
          jmp_buf mEnv;
        };
      };
    }  // namespace base
  }  // namespace fibers
}  // namespace span

#endif  // SPAN_SRC_SPAN_FIBERS_BASE_UNIXFIBERBASE_HH_
