#include <atomic>
#include <vector>

#ifdef HAVE_VALGRIND
#include <valgrind/valgrind.h>
#endif

#include "span/Common.hh"
#include "span/exceptions/Assert.hh"
#include "span/fibers/Fiber.hh"

#include "absl/synchronization/mutex.h"

#if PLATFORM == PLATFORM_DARWIN
#define setjmp _setjmp
#define longjmp _longjmp
#endif

namespace span {
  namespace fibers {
    static size_t globalPageSize;

    namespace {
      static struct FiberInitializer {
        FiberInitializer() {
#if PLATFORM == PLATFORM_WIN32
          SYSTEM_INFO info;
          GetSystemInfo(&info);
          globalPageSize = info.dwPageSize;
#elif defined(POSIX)
          globalPageSize = sysconf(_SC_PAGESIZE);
#endif
        }
      } globalInit;
    }  // namespace

#if PLATFORM == PLATFORM_WIN32
#define DEFAULT_STACK_SIZE 0
#else
#define DEFAULT_STACK_SIZE 1024 * 1024u
#endif

    thread_local Fiber* Fiber::fiber = nullptr;
    static thread_local Fiber::ptr threadFiber;

    static ::absl::Mutex & globalFlsMutex() {
      static ::absl::Mutex mutex;
      return mutex;
    }

    static std::vector<bool> & globalFlsIndicides() {
      static std::vector<bool> indices;
      return indices;
    }

    Fiber::Fiber() : sp(stackId()), currentState(EXEC) {
      SPAN_ASSERT(!fiber);
      setThis(this);
    }

    Fiber::Fiber(std::function<void()> dg, size_t stackSize) :
      base::FiberBase(stackSize ? stackSize : DEFAULT_STACK_SIZE),
      dg(dg),
      sp(stackId()),
      currentState(INIT) {
    }

    Fiber::~Fiber() noexcept(false) {
      if (!stackPtr()) {
        // Thread Entry Fiber.
        SPAN_ASSERT(!dg);
        SPAN_ASSERT(currentState == EXEC);
        Fiber *cur = fiber;

        // We're actually running on the fiber we're about to delete.
        // i.e. thread is dying, clean up after ourselves.
        if (cur == this) {
          setThis(NULL);
#if PLATFORM == PLATFORM_WIN32
          if (stack) {
            SPAN_ASSERT(stack == sp);
            SPAN_ASSERT(stack == GetCurrentFiber());
            pConvertFiberToThread();
          }
#endif
        }
      } else {
        SPAN_ASSERT(currentState == TERM || currentState == INIT || currentState == EXCEPT);
      }
    }

    void Fiber::reset(std::function<void()> pDg) {
      exception = std::exception_ptr();
      SPAN_ASSERT(stackPtr() != nullptr);
      SPAN_ASSERT(currentState == TERM || currentState == INIT || currentState == EXCEPT);
      dg = pDg;
      base::FiberBase::reset();
      currentState = INIT;
    }

    Fiber::ptr Fiber::getThis() {
      if (fiber) {
        return fiber->shared_from_this();
      }

      Fiber::ptr p_threadFiber(new Fiber());
      SPAN_ASSERT(fiber == p_threadFiber.get());
      threadFiber = p_threadFiber;
      return fiber->shared_from_this();
    }

    void Fiber::setThis(Fiber* f) {
      fiber = f;
    }

    void Fiber::call() {
      SPAN_ASSERT(!outer);
      ptr cur = getThis();
      SPAN_ASSERT(currentState == HODL || currentState == INIT);
      SPAN_ASSERT(cur);
      SPAN_ASSERT(cur.get() != this);
      setThis(this);
      outer = cur;
      currentState = exception ? EXCEPT : EXEC;
      cur->switchContext(this);
      setThis(cur.get());
      SPAN_ASSERT(cur->yielder);
      outer.reset();
      if (cur->yielder) {
        SPAN_ASSERT(cur->yielder.get() == this);
        Fiber::ptr yielder = cur->yielder;
        yielder->currentState = cur->yielderNextState;
        cur->yielder.reset();
        if (yielder->currentState == EXCEPT) {
          std::rethrow_exception(exception);
        }
      }
      SPAN_ASSERT(cur->currentState == EXEC);
    }

    void Fiber::inject(std::exception_ptr ep) {
      SPAN_ASSERT(ep);
      exception = ep;
      call();
    }

    Fiber::ptr Fiber::yieldTo(bool yieldToCallerOnTerminate) {
      return yieldTo(yieldToCallerOnTerminate, HODL);
    }

    void Fiber::yield() {
      ptr cur = getThis();
      SPAN_ASSERT(cur);
      SPAN_ASSERT(cur->currentState == EXEC);
      SPAN_ASSERT(cur->outer);
      cur->outer->yielder = cur;
      cur->outer->yielderNextState = Fiber::HODL;
      cur->switchContext(cur->outer.get());
      if (cur->yielder) {
        cur->yielder->currentState = cur->yielderNextState;
        cur->yielder.reset();
      }
      if (cur->currentState == EXCEPT) {
        SPAN_ASSERT(cur->exception);
        std::rethrow_exception(cur->exception);
      }
      SPAN_ASSERT(cur->currentState == EXEC);
    }

    Fiber::State Fiber::state() {
      return currentState;
    }

    Fiber::ptr Fiber::yieldTo(bool yieldToCallerOnTerminate, State targetState) {
      SPAN_ASSERT(currentState == HODL || currentState == INIT);
      SPAN_ASSERT(targetState == HODL || targetState == TERM || targetState == EXCEPT);
      ptr cur = getThis();
      SPAN_ASSERT(cur);
      setThis(this);
      if (yieldToCallerOnTerminate) {
        Fiber::ptr outer = shared_from_this();
        Fiber::ptr previous;
        while (outer) {
          previous = outer;
          outer = outer->outer;
        }
        previous->terminateOuter = cur;
      }
      currentState = EXEC;
      yielder = cur;
      yielderNextState = targetState;
      Fiber *curp = cur.get();
      // Religuish our reference.
      cur.reset();
      curp->switchContext(this);
#if PLATFORM == PLATFORM_WIN32
      if (targetState == TERM) {
        return Fiber::ptr();
      }
#endif
      SPAN_ASSERT(targetState != TERM);
      setThis(curp);
      if (curp->yielder) {
        Fiber::ptr yielder = curp->yielder;
        yielder->currentState = curp->yielderNextState;
        curp->yielder.reset();
        if (yielder->exception) {
          std::rethrow_exception(yielder->exception);
        }
        return yielder;
      }
      if (curp->currentState == EXCEPT) {
        SPAN_ASSERT(curp->exception);
        std::rethrow_exception(curp->exception);
      }
      SPAN_ASSERT(curp->currentState == EXEC);
      return Fiber::ptr();
    }

    void Fiber::entrypoint() {
      ptr cur = getThis();
      SPAN_ASSERT(cur);
      if (cur->yielder) {
        cur->yielder->currentState = cur->yielderNextState;
        cur->yielder.reset();
      }
      SPAN_ASSERT(cur->dg);
      State nextState = TERM;
      try {
        if (cur->currentState == EXCEPT) {
          SPAN_ASSERT(cur->exception);
          std::rethrow_exception(cur->exception);
        }
        SPAN_ASSERT(cur->currentState == EXEC);
        cur->dg();
        cur->dg = NULL;
      } catch (...) {
        cur->exception = std::current_exception();
        nextState = EXCEPT;
      }

      exitpoint(&cur, nextState);
    }

    void Fiber::exitpoint(Fiber::ptr *cur, State targetState) {
      Fiber::ptr outer;
      Fiber *rawPtr = NULL;
      auto _cur = cur->get();
      if (!_cur->terminateOuter.expired() && !_cur->outer) {
        outer = _cur->terminateOuter.lock();
        rawPtr = outer.get();
      } else {
        outer = _cur->outer;
        rawPtr = _cur;
      }
      SPAN_ASSERT(outer);
      SPAN_ASSERT(rawPtr);
      SPAN_ASSERT(outer != *cur);

      // Have to set this reference before calling yieldTo()
      // so we can reset cur before we call yieldTo()
      // (since it's not ever going to destruct.)
      outer->yielder = *cur;
      outer->yielderNextState = targetState;
      SPAN_ASSERT(!cur->unique());
      cur->reset();
      if (rawPtr == outer.get()) {
        rawPtr = outer.get();
        SPAN_ASSERT(!outer.unique());
        outer.reset();
        rawPtr->yieldTo(false, targetState);
      } else {
        outer.reset();
        rawPtr->switchContext(rawPtr->outer.get());
      }
    }

#if PLATFORM == PLATFORM_WIN32
static bool globalDoesntHaveOSFLS;
#endif

    size_t Fiber::flsAlloc() {
#if PLATFORM == PLATFORM_WIN32
      while (!globalDoesntHaveOSFLS) {
        size_t result = pFlsAlloc(NULL);
        if (result == FLS_OUT_OF_INDEXES && lastError() == ERROR_CALL_NOT_IMPLEMENTED) {
          globalDoesntHaveOSFLS = true;
          break;
        }
        if (result == FLS_OUT_OF_INDEXES) {
          throw std::current_exception();
        }
        return result;
      }
#endif
      absl::MutexLock lock(&globalFlsMutex());
      std::vector<bool>::iterator it = std::find(globalFlsIndicides().begin(),
        globalFlsIndicides().end(), false);
      // TODO(kfc): We don't clear out values when free'ing, so we can't reuse force new.
      it = globalFlsIndicides().end();
      if (it == globalFlsIndicides().end()) {
        globalFlsIndicides().resize(globalFlsIndicides().size() + 1);
        globalFlsIndicides()[globalFlsIndicides().size() - 1] = true;
        return globalFlsIndicides().size() - 1;
      } else {
        size_t result = it - globalFlsIndicides().begin();
        globalFlsIndicides()[result] = true;
        return result;
      }
    }

    void Fiber::flsFree(size_t key) {
#if PLATFORM == PLATFORM_WIN32
      if (!globalDoesntHaveOSFLS) {
        if (!pFlsFree((DWORD) key)) {
          throw std::current_exception();
        }
        return;
      }
#endif
      absl::MutexLock lock(&globalFlsMutex());
      SPAN_ASSERT(key < globalFlsIndicides().size());
      SPAN_ASSERT(globalFlsIndicides()[key]);
      if (key + 1 == globalFlsIndicides().size()) {
        globalFlsIndicides().resize(key);
      } else {
        // TODO(kfc): Clear out current values.
        globalFlsIndicides()[key] = false;
      }
    }

    void Fiber::flsSet(size_t key, intptr_t value) {
#if PLATFORM == PLATFORM_WIN32
      if (!globalDoesntHaveOSFLS) {
        if (!pFlsSetValue((DWORD) key, (PVOID) value)) {
          throw std::current_exception();
        }
        return;
      }
#endif
      Fiber::ptr self = Fiber::getThis();
      if (self->fls.size() <= key) {
        self->fls.resize(key + 1);
      }
      self->fls[key] = value;
    }

    intptr_t Fiber::flsGet(size_t key) {
#if PLATFORM == PLATFORM_WIN32
      if (!globalDoesntHaveOSFLS) {
        error_t error = lastError();
        intptr_t result = (intptr_t) pFlsGetValue((DWORD) key);
        lastError(error);
        return result;
      }
#endif
      Fiber::ptr self = Fiber::getThis();
      if (self->fls.size() <= key) {
        return 0;
      }
      return self->fls[key];
    }
  }  // namespace fibers
}  // namespace span
