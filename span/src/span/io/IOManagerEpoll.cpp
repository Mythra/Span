#include "span/io/IOManagerEpoll.hh"

#if PLATFORM == PLATFORM_UNIX
#if UNIX_FLAVOUR != UNIX_FLAVOUR_BSD && UNIX_FLAVOUR != UNIX_FLAVOUR_OSX

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#include <exception>
#include <vector>

#include "span/fibers/Fiber.hh"
#include "span/exceptions/Assert.hh"

#include "glog/logging.h"

// EPOLLRDHUP is missing in the header on etch
#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif

namespace span {
  namespace io {
    enum epoll_ctl_op_t {
      epoll_ctl_op_t_dummy = 0x7ffffff
    };

    static std::ostream &operator <<(std::ostream &os, epoll_ctl_op_t op) {
      switch (static_cast<int>(op)) {
      case EPOLL_CTL_ADD:
        return os << "EPOLL_CTL_ADD";
      case EPOLL_CTL_MOD:
        return os << "EPOLL_CTL_MOD";
      case EPOLL_CTL_DEL:
        return os << "EPOLL_CTL_DEL";
      default:
        return os << static_cast<int>(op);
      }
    }

    static std::ostream &operator <<(std::ostream &os, EPOLL_EVENTS events) {
      if (!events) {
        return os << '0';
      }
      bool one = false;
      if (events & EPOLLIN) {
        os << "EPOLLIN";
        one = true;
      }
      if (events & EPOLLOUT) {
        if (one) {
          os << " | ";
        }
        os << "EPOLLOUT";
        one = true;
      }
      if (events & EPOLLPRI) {
        if (one) {
          os << " | ";
        }
        os << "EPOLLPRI";
        one = true;
      }
      if (events & EPOLLERR) {
        if (one) {
          os << " | ";
        }
        os << "EPOLLERR";
        one = true;
      }
      if (events & EPOLLHUP) {
        if (one) {
          os << " | ";
        }
        os << "EPOLLHUP";
        one = true;
      }
      if (events & EPOLLET) {
        if (one) {
          os << " | ";
        }
        os << "EPOLLET";
        one = true;
      }
      if (events & EPOLLONESHOT) {
        if (one) {
          os << " | ";
        }
        os << "EPOLLONESHOT";
        one = true;
      }
      if (events & EPOLLRDHUP) {
        if (one) {
          os << " | ";
        }
        os << "EPOLLRDHUP";
        one = true;
      }
      events = (EPOLL_EVENTS)(
        events & ~(EPOLLIN | EPOLLOUT | EPOLLPRI | EPOLLERR | EPOLLHUP | EPOLLET | EPOLLONESHOT | EPOLLRDHUP));
      if (events) {
        if (one) {
          os << " | ";
        }
        os << static_cast<uint32>(events);
      }
      return os;
    }

    IOManager::AsyncState::AsyncState() : fd(0), events(NONE) {}

    IOManager::AsyncState::~AsyncState() noexcept(false) {
      absl::MutexLock lock(&mutex);
      SPAN_ASSERT(!events);
    }

    IOManager::AsyncState::EventContext &IOManager::AsyncState::contextForEvent(Event event) {
      switch (event) {
        case READ:
          return in;
        case WRITE:
          return out;
        case CLOSE:
          return close;
        default:
          SPAN_ASSERT(false);  // Not possible to reach.
      }
    }

    bool IOManager::AsyncState::triggerEvent(Event event, std::atomic<size_t> *pendingEventCount) {
      if (!(events & event)) {
        return false;
      }
      events = (Event)(events & ~event);
      --*pendingEventCount;
      EventContext &context = contextForEvent(event);
      if (context.dg) {
        context.scheduler->schedule(&context.dg);
      } else {
        context.scheduler->schedule(&context.fiber);
      }
      context.scheduler = NULL;
      return true;
    }

    void IOManager::AsyncState::asyncResetContext(AsyncState::EventContext& context) {
      // fiber.reset is not necessary to be running under the lock.
      // However it is needed to acquire the lock, and then unlock
      // to ensure that this function is executed after the other
      // fiber which schedule this async reset call.
      {
        absl::MutexLock lock(&mutex);
      }
      context.fiber.reset();
      context.dg = NULL;
    }

    void IOManager::AsyncState::resetContext(EventContext &context) {
      // asynchronously reset fiber/dg to avoid destroying in IOManager::idle.
      // NOTE: this function has the pre-condition that the mutex is already acquired in upper level
      // (which is true right now), in this way the asyncReset will not be executed until the mutex
      // is released, and it is surely run in the Scheduler working fiber instead of the idle fiber.
      // It is fine to pass context address to the function since the address will always be valid
      // until ~IOManager()
      context.scheduler->schedule(std::bind(&IOManager::AsyncState::asyncResetContext, this, context));
      context.scheduler = NULL;
      context.fiber.reset();
      context.dg = NULL;
    }

    IOManager::IOManager(size_t threads, bool useCaller, bool autoStart) : span::fibers::Scheduler(threads, useCaller),
      pendingEventCount(0) {
      epfd = epoll_create(5000);
      if (epfd <= 0) {
        LOG(ERROR) << this << " epoll_create(5000): " << epfd;
        throw std::current_exception();
      } else {
        LOG(INFO) << this << " epoll_create(5000): " << epfd;
      }
      int rc = pipe(tickleFds);
      if (rc) {
        LOG(ERROR) << this << " pipe(): " << rc;
        close(epfd);
        throw std::current_exception();
      } else {
        LOG(INFO) << this << " pipe(): " << rc;
      }

      SPAN_ASSERT(tickleFds[0] > 0);
      SPAN_ASSERT(tickleFds[1] > 0);

      epoll_event event;
      memset(&event, 0, sizeof(epoll_event));
      event.events = EPOLLIN | EPOLLET;
      event.data.fd = tickleFds[0];
      rc = fcntl(tickleFds[0], F_SETFL, O_NONBLOCK);
      if (rc == -1) {
        close(tickleFds[0]);
        close(tickleFds[1]);
        close(epfd);
        throw std::current_exception();
      }

      rc = epoll_ctl(epfd, EPOLL_CTL_ADD, tickleFds[0], &event);
      if (rc) {
        LOG(ERROR) << this << " epoll_ctl(" << epfd << ", EPOLL_CTL_ADD," << tickleFds[0]
          << ", EPOLLIN | EPOLLET): " << rc;
        close(tickleFds[0]);
        close(tickleFds[1]);
        close(epfd);
        throw std::current_exception();
      } else {
        LOG(INFO) << this << " epoll_ctl(" << epfd << ", EPOLL_CTL_ADD," << tickleFds[0]
          << ", EPOLLIN | EPOLLET): " << rc;
      }

      if (autoStart) {
        try {
          start();
        } catch (...) {
          close(tickleFds[0]);
          close(tickleFds[1]);
          close(epfd);
          throw;
        }
      }
    }

    IOManager::~IOManager() noexcept(false) {
      stop();
      close(epfd);
      LOG(INFO) << this << " close(" << epfd << ")";
      close(tickleFds[0]);
      LOG(INFO) << this << " close(" << tickleFds[0] << ")";
      close(tickleFds[1]);
      // Yes it would be more C++-esque to store a std::shared_ptr in the vec,
      // but that would require an extra alloc per fd for the counter.
      for (size_t i = 0; i < pendingEvents.size(); ++i) {
        if (pendingEvents[i]) {
          delete pendingEvents[i];
        }
      }
    }

    bool IOManager::stopping() {
      uint64 timeout;
      return stopping(&timeout);
    }

    void IOManager::registerEvent(int fd, Event event, std::function<void()> dg) {
      SPAN_ASSERT(fd > 0);
      SPAN_ASSERT(Scheduler::getThis());
      SPAN_ASSERT(dg || span::fibers::Fiber::getThis());
      SPAN_ASSERT(event == READ || event == WRITE || event == CLOSE);

      // Look up our state in the global map, expanding it if necessary.
      AsyncState *state = nullptr;
      {
        absl::MutexLock lock(&mutex);
        if (pendingEvents.size() < static_cast<size_t>(fd)) {
          pendingEvents.resize(fd * 3 / 2);
        }
        if (!pendingEvents[fd - 1]) {
          pendingEvents[fd - 1] = new AsyncState();
          pendingEvents[fd - 1]->fd = fd;
        }
        state = pendingEvents[fd - 1];
        SPAN_ASSERT(fd == state->fd);
      }
      absl::MutexLock lock(&state->mutex);

      SPAN_ASSERT(!(state->events & event));
      int op = state->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
      epoll_event epevent;
      epevent.events = EPOLLET | state->events | event;
      epevent.data.ptr = state;

      int rc = epoll_ctl(epfd, op, fd, &epevent);
      if (rc) {
        LOG(ERROR) << this << " epoll_ctl(" << epfd << ", " << (epoll_ctl_op_t)op << ", "
          << fd << ", " << (EPOLL_EVENTS)epevent.events << "): " << rc;
          throw std::current_exception();
      } else {
        LOG(INFO) << this << " epoll_ctl(" << epfd << ", " << (epoll_ctl_op_t)op << ", "
          << fd << ", " << (EPOLL_EVENTS)epevent.events << "): " << rc;
      }
      pendingEventCount++;
      state->events = (Event)(state->events | event);
      AsyncState::EventContext &context = state->contextForEvent(event);
      SPAN_ASSERT(!context.scheduler);
      SPAN_ASSERT(!context.fiber);
      SPAN_ASSERT(!context.dg);
      context.scheduler = Scheduler::getThis();
      if (dg) {
        context.dg.swap(dg);
      } else {
        context.fiber = span::fibers::Fiber::getThis();
      }
    }

    bool IOManager::unregisterEvent(int fd, Event event) {
      SPAN_ASSERT(fd > 0);
      SPAN_ASSERT(event == READ || event == WRITE || event == CLOSE);

      AsyncState *state = nullptr;
      {
        absl::MutexLock lock(&mutex);
        if (pendingEvents.size() < static_cast<size_t>(fd)) {
          return false;
        }
        if (!pendingEvents[fd - 1]) {
          return false;
        }
        state = pendingEvents[fd - 1];
        SPAN_ASSERT(fd == state->fd);
      }

      absl::MutexLock lock(&state->mutex);
      if (!(state->events & event)) {
        return false;
      }

      SPAN_ASSERT(fd == state->fd);
      Event newEvents = (Event)(state->events &~event);
      int op = newEvents ? EPOLL_CTL_MOD: EPOLL_CTL_DEL;
      epoll_event epevent;
      epevent.events = EPOLLET | newEvents;
      epevent.data.ptr = state;

      int rc = epoll_ctl(epfd, op, fd, &epevent);
      if (rc) {
        LOG(ERROR) << this << " epoll_ctl(" << epfd << ", " << (epoll_ctl_op_t)op << ", "
          << fd << ", " << (EPOLL_EVENTS)epevent.events << "): " << rc;
        throw std::current_exception();
      } else {
        LOG(INFO) << this << " epoll_ctl(" << epfd << ", " << (epoll_ctl_op_t)op << ", "
          << fd << ", " << (EPOLL_EVENTS)epevent.events << "): " << rc;
      }
      pendingEventCount--;
      state->events = newEvents;
      AsyncState::EventContext &context = state->contextForEvent(event);
      // spawn a dedicated fiber to do the cleanup.
      state->resetContext(context);
      return true;
    }

    bool IOManager::cancelEvent(int fd, Event event) {
      SPAN_ASSERT(fd > 0);
      SPAN_ASSERT(event == READ || event == WRITE || event == CLOSE);

      AsyncState *state = nullptr;
      {
        absl::MutexLock lock(&mutex);
        if (pendingEvents.size() < static_cast<size_t>(fd)) {
          return false;
        }
        if (!pendingEvents[fd - 1]) {
          return false;
        }
        state = pendingEvents[fd - 1];
        SPAN_ASSERT(fd == state->fd);
      }
      absl::MutexLock(&state->mutex);
      if (!(state->events & event)) {
        return false;
      }

      SPAN_ASSERT(fd == state->fd);
      Event newEvents = (Event)(state->events &~event);
      int op = newEvents ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
      epoll_event epevent;
      epevent.events = EPOLLET | newEvents;
      epevent.data.ptr = state;

      int rc = epoll_ctl(epfd, op, fd, &epevent);
      if (rc) {
        LOG(ERROR) << this << " epoll_ctl(" << epfd << ", " << (epoll_ctl_op_t)op << ", "
          << fd << ", " << (EPOLL_EVENTS)epevent.events << "): " << rc;
        throw std::current_exception();
      } else {
        LOG(INFO) << this << " epoll_ctl(" << epfd << ", " << (epoll_ctl_op_t)op << ", "
          << fd << ", " << (EPOLL_EVENTS)epevent.events << "): " << rc;
      }
      state->triggerEvent(event, &pendingEventCount);
      return true;
    }

    bool IOManager::stopping(uint64 *nextTimeout) {
      *nextTimeout = nextTimer();
      return *nextTimeout == ~0ull && Scheduler::Stopping() && pendingEventCount == 0;
    }

    void IOManager::idle() {
      epoll_event events[64];
      while (true) {
        uint64 nextTimeout;
        if (stopping(&nextTimeout)) {
          return;
        }
        int rc;
        int timeout;
        do {
          if (nextTimeout != ~0ull) {
            timeout = static_cast<int>((nextTimeout / 1000) + 1);
          } else {
            timeout = -1;
          }
          rc = epoll_wait(epfd, events, 64, timeout);
          if (rc < 0 && errno == EINTR) {
            nextTimeout = nextTimer();
          } else {
            break;
          }
        } while (true);

        if (rc < 0) {
          LOG(ERROR) << this << " epoll_wait(" << epfd << ", 64, " << timeout << "): " << rc;
          throw std::current_exception();
        } else {
          LOG(INFO) << this << " epoll_wait(" << epfd << ", 64, " << timeout << "): " << rc;
        }
        std::vector<std::function<void()>> expired = processTimers();
        if (!expired.empty()) {
          schedule(expired.begin(), expired.end());
          expired.clear();
        }

        std::exception_ptr exception;
        for (int i = 0; i < rc; ++i) {
          epoll_event &event = events[i];
          if (event.data.fd == tickleFds[0]) {
            unsigned char dummy;
            int rc2;
            while ((rc2 = read(tickleFds[0], &dummy, 1)) == 1) {
              LOG(INFO) << this << " received tickle";
            }
            SPAN_ASSERT(rc2 < 0 && errno == EAGAIN);
            continue;
          }

          AsyncState *state = static_cast<AsyncState *>(event.data.ptr);

          absl::MutexLock lock2(&state->mutex);
          LOG(INFO) << " epoll_event {" << (EPOLL_EVENTS)event.events << ", " << state->fd
            << "}, registered for " << (EPOLL_EVENTS)state->events;

          if (event.events & (EPOLLERR | EPOLLHUP)) {
            event.events |= EPOLLIN | EPOLLOUT;
          }

          int incomingEvents = NONE;
          if (event.events & EPOLLIN) {
            incomingEvents = READ;
          }
          if (event.events & EPOLLOUT) {
            incomingEvents |= WRITE;
          }
          if (event.events & EPOLLRDHUP) {
            incomingEvents |= CLOSE;
          }

          // Nothing will be triggered, probably because a prior cancelEvent call
          // (probably on a different thread) already triggered it, so no
          // need to tell epoll anything.
          if ((state->events & incomingEvents) == NONE) {
            continue;
          }

          int remainingEvents = (state->events & ~incomingEvents);
          int op = remainingEvents ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
          event.events = EPOLLET | remainingEvents;

          int rc2 = epoll_ctl(epfd, op, state->fd, &event);
          if (rc2) {
            LOG(ERROR) << this << " epoll_ctl(" << epfd << ", " << (epoll_ctl_op_t)op << ", "
              << state->fd << ", " << (EPOLL_EVENTS)event.events << "): " << rc2;
            exception = std::current_exception();
          } else {
            LOG(INFO) << this << " epoll_ctl(" << epfd << ", " << (epoll_ctl_op_t)op << ", "
              << state->fd << ", " << (EPOLL_EVENTS)event.events << "): " << rc2;
          }

          bool triggered = false;
          if (incomingEvents & READ) {
            triggered = state->triggerEvent(READ, &pendingEventCount);
          }
          if (incomingEvents & WRITE) {
            triggered = state->triggerEvent(WRITE, &pendingEventCount) || triggered;
          }
          if (incomingEvents & CLOSE) {
            triggered = state->triggerEvent(CLOSE, &pendingEventCount) || triggered;
          }
          SPAN_ASSERT(triggered);
        }

        if (exception) {
          throw exception;
        }

        try {
          span::fibers::Fiber::yield();
        } catch (...) {
          return;
        }
      }
    }

    void IOManager::tickle() {
      if (!hasIdleThreads()) {
        LOG(INFO) << this << " 0 idle threads, no tickle.";
        return;
      }
      int rc = write(tickleFds[1], "T", 1);
      LOG(INFO) << this << " write(" << tickleFds[1] << ", 1): " << rc;
      SPAN_ASSERT(rc == 1);
    }

  }  // namespace io
}  // namespace span

#endif
#endif
