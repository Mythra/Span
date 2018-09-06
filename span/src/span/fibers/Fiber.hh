#ifndef SPAN_SRC_SPAN_FIBERS_FIBER_HH_
#define SPAN_SRC_SPAN_FIBERS_FIBER_HH_

#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

#include "span/Common.hh"

#if PLATFORM == PLATFORM_WIN32
#error Your platform (architecture and compiler) is NOT supported for Fibers. Yell at someone to add WindowsFiber.
#else
#include "span/fibers/base/UnixFiberBase.hh"
#endif

namespace span {
  namespace fibers {
    class Fiber final : public std::enable_shared_from_this<Fiber>, private base::FiberBase {
      template <class T> friend class FiberLocalStorageBase;
    public:
      typedef std::shared_ptr<Fiber> ptr;
      typedef std::weak_ptr<Fiber> weak_ptr;

      // Current State of the Fiber.
      enum State {
        // Initalized but not run.
        INIT,
        // Currently Supsended, yes this is mispelled on purpose.
        HODL,
        // Running
        EXEC,
        // Exception
        EXCEPT,
        // Terminated
        TERM
      };

      // Creates a new Fiber.
      //
      // * `dg` - The Initial Function.
      // * `stacksize` - An explicit size for the stack. This is the initial size in virtual
      //    memory; physical/paging memory is not allocated until the actual pages are touched
      //    by the fiber executing.
      //
      // Afterwards the state is INIT.
      explicit Fiber(std::function<void()> dg, size_t stackSize = 0);
      ~Fiber() noexcept(false);

      // Resets a Fiber to be used again, but with a different function.
      //
      // * `dg` - The new initial function.
      //
      // The pre state should be one of INIT/TERM/EXCEPT, post state will be INIT.
      void reset(std::function<void()> dg);

      // Get the current executing Fiber.
      static ptr getThis();

      // Calls a Fiber.
      //
      // The Fiber is executed as a "child" Fiber of the currently executing Fiber. The Currently
      // Executing Fiber is left in the "EXEC" state, and this fiber also transitions to the EXEC
      // state by either calling the initial function or returning from yield() or yieldTo().
      //
      // call() does not return until the Fiber calls yield(), returns, or throws.
      //
      // The pre state needs to be one of INIT/HODL.
      void call();

      /// Injects an Exception into a Fiber.
      ///
      /// The fiber is executed but instead of returning to yield(), or yieldTo()
      /// exception is rethrown in the fiber.
      ///
      /// The pre state needs to be one of INIT/HODL.
      void inject(std::exception_ptr exception);

      // Yield execution to a specific fiber.
      //
      // * `yieldToCallerOnTerminate` - Whether to keep a weak reference back to the
      //   currently executing fiber in order to yield back when the yielded function
      //   terminates.
      //
      // The Fiber is executed by replacing the currently executing Fiber.
      // The currently executing Fiber transitions into the HODL state, and this
      // Fiber transitions to the EXEC State, by either calling the initial function, or
      // returning from yield() or yieldTo().
      //
      // yieldTo() does not return until another fiber calls yieldTo() on the currently
      // executing Fiber, or yieldToCallerOnTerminate is true, and this fiber returns or
      // throws.
      //
      // Returns the fiber that yielded back which may not be the one you yielded to.
      //
      // The Pre State should be INIT or HODL.
      Fiber::ptr yieldTo(bool yieldToCallerOnTerminate = true);

      // Yield to the calling Fiber.
      //
      // yield() returns when the Fiber has been called, or yielded to again.
      //
      // The function should have already had call(), well called.
      static void yield();

      // The Current Execution State of this Fiber.
      State state();

    private:
      // Create a Fiber for the current executing thread.
      //
      // No Other Fiber Object represents the current executing thread.
      // Should set state to EXEC.
      Fiber();

      Fiber::ptr yieldTo(bool yieldToCallerOnTerminate, State targetState);

      static void setThis(Fiber *f);

      virtual void entrypoint();
      static void exitpoint(Fiber::ptr *cur, State targetState);

      Fiber(const Fiber& rhs) = delete;

      std::function<void()> dg;
      void *sp;
      State currentState, yielderNextState;
      ptr outer, yielder;
      weak_ptr terminateOuter;
      std::exception_ptr exception;

      static thread_local Fiber* fiber;

      // Support for fiber local storage.
      static size_t flsAlloc();
      static void flsFree(size_t key);
      static void flsSet(size_t key, intptr_t val);
      static intptr_t flsGet(size_t key);

      std::vector<intptr_t> fls;
    };

    template<class T>
    class FiberLocalStorageBase {
    public:
      FiberLocalStorageBase() {
        key = Fiber::flsAlloc();
      }

      ~FiberLocalStorageBase() {
        Fiber::flsFree(key);
      }

      void set(const T &t) {
        Fiber::flsSet(key, (intptr_t) t);
      }

      T get() const {
#if PLATFORM == PLATFORM_WIN32
#pragma warning(push)
#pragma warning(disable: 4800)
#endif
        return (T) Fiber::flsGet(key);
#if PLATFORM == PLATFORM_WIN32
#pragma warning(pop)
#endif
      }

      operator T() const {
        return get();
      }

    private:
      FiberLocalStorageBase(const FiberLocalStorageBase &rhs) = delete;
      size_t key;
    };

    template<class T>
    class FiberLocalStorage : public FiberLocalStorageBase<T> {
    public:
      T operator =(T t) {
        FiberLocalStorageBase<T>::set(t);
        return t;
      }
    };

    template<class T>
    class FiberLocalStorage<T *> : public FiberLocalStorageBase<T *> {
    public:
      T * operator =(T *const t) {
        FiberLocalStorageBase<T *>::set(t);
        return t;
      }

      T & operator*() {
        return *FiberLocalStorageBase<T *>::get();
      }

      T * operator->() {
        return FiberLocalStorageBase<T *>::get();
      }
    };
  }  // namespace fibers
}  // namespace span

#endif  // SPAN_SRC_SPAN_FIBERS_FIBER_HH_
