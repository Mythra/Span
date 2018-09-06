#include <cstdlib>
#include <new>

#include "span/exceptions/Assert.hh"
#include "span/fibers/base/UnixFiberBase.hh"

namespace span {
  namespace fibers {
    namespace base {
      FiberBase::FiberBase() : mInit(true), mStack(nullptr) { }

      FiberBase::FiberBase(uint32 size) : stackSize(size) {
        if ((mStack = malloc(stackSize)) == nullptr) {
          throw std::bad_alloc();
        }
#ifdef HAVE_VALGRIND
        mValgrindStackId = VALGRIND_STACK_REGISTER(mStack, (char *)mStack + stackSize);
#endif
        reset();
      }

      FiberBase::~FiberBase() {
#ifdef HAVE_VALGRIND
        VALGRIND_STACK_DEREGISTER(mValgrindStackId);
#endif
        free(mStack);
      }

      void FiberBase::reset() {
        ucontext_t tmp;
        mInit = false;

        if (getcontext(&mCtx) == -1) {
          throw std::current_exception();
        }

        mCtx.uc_stack.ss_sp = mStack;
        mCtx.uc_stack.ss_size = stackSize;
        // Save tmp, so we can trampoline back.
        mCtx.uc_link = &tmp;

        // Jump to Trampoline, and return right before calling entrypoint.
        makecontext(&mCtx, ((void(*)()) FiberBase::trampoline), 1, this);
      }

      void FiberBase::switchContext(FiberBase *to) {
        if (!_setjmp(mEnv)) {
          // First switch into the thread uses stack prepared by makecontext.
          // After that we can use setjmp / longjmp for subsequent calls.
          if (to->mInit == false) {
            setcontext(&to->mCtx);
          } else {
            _longjmp(to->mEnv, 1);
          }
        }
      }

      void FiberBase::trampoline(void *ptr) {
        FiberBase* fiber = reinterpret_cast<FiberBase*>(ptr);

        fiber->mInit = true;
        fiber->entrypoint();
      }
    }  // namespace base
  }  // namespace fibers
}  // namespace span
