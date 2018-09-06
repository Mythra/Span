#include <exception>

#include "span/fibers/Semaphore.hh"
#include "span/exceptions/Assert.hh"

#if PLATFORM != PLATFORM_WIN32
#include <errno.h>
#endif

#if UNIX_FLAVOUR == UNIX_FLAVOUR_BSD
#include <sys/sem.h>
#endif

#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
#include <mach/mach_init.h>
#include <mach/task.h>
#endif

namespace span {
  namespace fibers {

    Semaphore::Semaphore(unsigned int count) {
#if PLATFORM == PLATFORM_WIN32
      sema = CreateSemaphore(NULL, count, 2147483647, NULL);
      if (sema == NULL) {
        throw std::current_exception();
      }
#elif PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
      task = mach_task_self();
      if (semaphore_create(task, &sema, SYNC_POLICY_FIFO, count)) {
        throw std::current_exception();
      }
#elif UNIX_FLAVOUR == UNIX_FLAVOUR_BSD
      sema = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
      if (sema < 0) {
        throw std::current_exception();
      }
      semun init;
      init.val = count;
      if (semctl(sema, 0, SETVAL, init) < 0) {
        semctl(sema, 0, IPC_RMID);
        throw std::current_exception();
      }
#else
      int rc = sem_init(&sema, 0, count);
      if (rc || rc == -1) {
        throw std::runtime_error("sem_init");
      }
#endif
    }

    Semaphore::~Semaphore() noexcept(false) {
#if PLATFORM == PLATFORM_WIN32
      SPAN_ASSERT(CloseHandle(sema));
#elif PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
      SPAN_ASSERT(!semaphore_destroy(task, sema));
#elif UNIX_FLAVOUR == UNIX_FLAVOUR_BSD
      SPAN_ASSERT(semctl(sema, 0, IPC_RMID) >= 0);
#else
      SPAN_ASSERT(!sem_destroy(&sema));
#endif
    }

    void Semaphore::wait() {
#if PLATFORM == PLATFORM_WIN32
      DWORD dwRet = WaitForSingleObject(sema, INFINITE);
      if (dwRet != WAIT_OBJECT_0) {
        throw std::current_exception();
      }
#elif PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
      kern_return_t rc;
      do {
        rc = semaphore_wait(sema);
      } while (rc == KERN_ABORTED);
      if (rc != KERN_SUCCESS) {
        throw std::current_exception();
      }
#elif UNIX_FLAVOUR == UNIX_FLAVOUR_BSD
      sembuf op;
      op.sem_num = 0;
      op.sem_op = -1;
      op.sem_flg = 0;
      while (true) {
        if (!semop(sema, &op, 1)) {
          return;
        }
        if (errno != EINTR) {
          throw std::current_exception();
        }
      }
#else
      while (true) {
        if (!sem_wait(&sema)) {
          return;
        }
        if (errno != EINTR) {
          throw std::current_exception();
        }
      }
#endif
    }

    void Semaphore::notify() {
#if PLATFORM == PLATFORM_WIN32
      if (!ReleaseSemaphore(sema, 1, NULL)) {
        throw std::current_exception();
      }
#elif PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
      semaphore_signal(sema);
#elif UNIX_FLAVOUR == UNIX_FLAVOUR_BSD
      sembuf op;
      op.sem_num = 0;
      op.sem_op = 1;
      op.sem_flg = 0;
      if (semop(sema, &op, 1)) {
        throw std::current_exception();
      }
#else
      if (sem_post(&sema)) {
        throw std::current_exception();
      }
#endif
    }

  }  // namespace fibers
}  // namespace span
