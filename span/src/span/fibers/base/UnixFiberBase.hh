#ifndef SPAN_SRC_SPAN_FIBERS_BASE_UNIXFIBERBASE_HH_
#define SPAN_SRC_SPAN_FIBERS_BASE_UNIXFIBERBASE_HH_

#include "span/Common.hh"

#include <setjmp.h>
#include <ucontext.h>

#ifdef HAVE_VALGRIND
#include <valgrind/valgrind.h>
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
