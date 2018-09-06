#ifndef SPAN_SRC_SPAN_FIBERS_WORKERPOOL_HH_
#define SPAN_SRC_SPAN_FIBERS_WORKERPOOL_HH_

#include "span/fibers/Scheduler.hh"
#include "span/fibers/Semaphore.hh"

namespace span {
  namespace fibers {
    class WorkerPool : public Scheduler {
    public:
      explicit WorkerPool(size_t threads = 1, bool useCaller = true, size_t batchSize = 1);

      ~WorkerPool() {
        stop();
      }

    protected:
      /// The Idle Fiber for a worker pool simply loops waitin on a Semaphore,
      /// and yields whenever that semaphore is signalled, returning if stopping() is true.
      void idle();
      /// Signals the semaphore so that the idle Fiber will yield.
      void tickle();

    private:
      Semaphore sema;
    };
  }  // namespace fibers
}  // namespace span

#endif  // SPAN_SRC_SPAN_FIBERS_WORKERPOOL_HH_
