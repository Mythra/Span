#include "span/io/IOManagerKqueue.hh"

#if UNIX_FLAVOUR == UNIX_FLAVOUR_BSD

#include <exception>
#include <map>
#include <utility>
#include <vector>

#include "span/exceptions/Assert.hh"
#include "span/fibers/Fiber.hh"

#include "glog/logging.h"

namespace span {
  namespace io {
    IOManager::IOManager(size_t threads, bool useCaller, bool autoStart) {
      kqfd = kqueue();
      if (kqfd <= 0) {
        LOG(ERROR) << this << " kqueue(): " << kqfd;
        throw std::current_exception();
      } else {
        LOG(INFO) << this << " kqueue(): " << kqfd;
      }
      int rc = pipe(tickleFds);
      if (rc) {
        LOG(ERROR) << this << " pipe(): " << rc;
        close(kqfd);
        throw std::current_exception();
      } else {
        LOG(INFO) << this << " pipe(): " << rc;
      }
      SPAN_ASSERT(tickleFds[0] > 0);
      SPAN_ASSERT(tickleFds[1] > 0);
      struct kevent event;
      EV_SET(&event, tickleFds[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
      rc = kevent(kqfd, &event, 1, NULL, 0, NULL);
      if (rc) {
        LOG(ERROR) << this << " kevent( " << kqfd << ", (" << tickleFds[0] << ", EVFILT_READ, EV_ADD)): " << rc;
        close(tickleFds[0]);
        close(tickleFds[1]);
        close(kqfd);
        throw std::current_exception();
      } else {
        LOG(INFO) << this << " kevent( " << kqfd << ", (" << tickleFds[0] << ", EVFILT_READ, EV_ADD)): " << rc;
      }
      if (autoStart) {
        try {
          start();
        } catch (...) {
          close(tickleFds[0]);
          close(tickleFds[1]);
          close(kqfd);
          throw;
        }
      }
    }

    IOManager::~IOManager() {
      stop();
      close(kqfd);
      LOG(INFO) << this << " close(" << kqfd << ")";
      close(tickleFds[0]);
      LOG(INFO) << this << " close(" << tickleFds[0] << ")";
      close(tickleFds[1]);
    }

    bool IOManager::stopping() {
      uint64 timeout;
      return stopping(&timeout);
    }

    void IOManager::registerEvent(int fd, Event events, std::function<void()> dg) {
      SPAN_ASSERT(fd > 0);
      SPAN_ASSERT(Scheduler::getThis());
      SPAN_ASSERT(fibers::Fiber::getThis());

      Event eventsKey = events;
      if (eventsKey == CLOSE) {
        eventsKey = READ;
      }
      absl::MutexLock lock(&mutex);
      AsyncEvent &e = pendingEvents[std::pair<int, Event>(fd, eventsKey)];

      memset(&e.event, 0, sizeof(struct kevent));
      e.event.ident = fd;
      e.event.flags = EV_ADD;
      switch (events) {
        case READ:
          e.event.filter = EVFILT_READ;
          break;
        case WRITE:
          e.event.filter = EVFILT_WRITE;
          break;
        case CLOSE:
          e.event.filter = EVFILT_READ;
          break;
        default:
          SPAN_ASSERT(false);  // Unreachable
      }

      if (events == READ || events == WRITE) {
        SPAN_ASSERT(!e.dg && !e.fiber);
        e.dg = dg;
        if (!dg) {
          e.fiber = fibers::Fiber::getThis();
        }
        e.scheduler = Scheduler::getThis();
      } else {
        SPAN_ASSERT(!e.dgClose && !e.fiberClose);
        e.dgClose = dg;
        if (!dg) {
          e.fiberClose = fibers::Fiber::getThis();
        }
        e.schedulerClose = Scheduler::getThis();
      }

      int rc = kevent(kqfd, &e.event, 1, NULL, 0, NULL);
      if (rc) {
        LOG(ERROR) << this << " kevent(" << kqfd << ", (" << fd << ", " << events << ", EV_ADD)): " << rc;
        throw std::current_exception();
      } else {
        LOG(INFO) << this << " kevent(" << kqfd << ", (" << fd << ", " << events << ", EV_ADD)): " << rc;
      }
    }

    void IOManager::cancelEvent(int fd, Event events) {
      Event eventsKey = events;
      if (eventsKey == CLOSE) {
        eventsKey = READ;
      }

      absl::MutexLock lock(&mutex);
      std::map<std::pair<int, Event>, AsyncEvent>::iterator it = pendingEvents.find(
        std::pair<int, Event>(fd, eventsKey));
      if (it == pendingEvents.end()) {
        return;
      }
      AsyncEvent &e = it->second;
      SPAN_ASSERT(e.event.ident == (unsigned) fd);

      Scheduler *scheduler;
      fibers::Fiber::ptr fiber;
      std::function<void()> dg;

      if (events == READ) {
        scheduler = e.scheduler;
        fiber.swap(e.fiber);
        dg.swap(e.dg);
        if (e.fiberClose || e.dgClose)  {
          if (dg || fiber) {
            if (dg) {
              scheduler->schedule(dg);
            } else {
              scheduler->schedule(fiber);
            }
          }
          return;
        }
      } else if (events == CLOSE) {
        scheduler = e.schedulerClose;
        fiber.swap(e.fiberClose);
        dg.swap(e.dgClose);
        if (e.fiber || e.dg) {
          if (dg || fiber) {
            if (dg) {
              scheduler->schedule(dg);
            } else {
              scheduler->schedule(fiber);
            }
          }
          return;
        }
      } else if (events == WRITE) {
        scheduler = e.scheduler;
        fiber.swap(e.fiber);
        dg.swap(e.dg);
      } else {
        SPAN_ASSERT(false);  // Not Reached.
      }

      e.event.flags = EV_DELETE;
      int rc = kevent(kqfd, &e.event, 1, NULL, 0, NULL);

      if (rc) {
        LOG(ERROR) << this << " kevent(" << kqfd << ", (" << fd << ", " << eventsKey << ", EV_DELETE)): " << rc;
        throw std::current_exception();
      } else {
        LOG(INFO) << this << " kevent(" << kqfd << ", (" << fd << ", " << eventsKey << ", EV_DELETE)): " << rc;
      }

      if (dg) {
        scheduler->schedule(&dg);
      } else {
        scheduler->schedule(&fiber);
      }

      pendingEvents.erase(it);
    }

    void IOManager::unregisterEvent(int fd, Event events) {
      Event eventsKey = events;
      if (eventsKey == CLOSE) {
        eventsKey = READ;
      }
      absl::MutexLock lock(&mutex);
      std::map<std::pair<int, Event>, AsyncEvent>::iterator it = pendingEvents.find(
        std::pair<int, Event>(fd, eventsKey));

      if (it == pendingEvents.end()) {
        return;
      }

      AsyncEvent &e = it->second;
      SPAN_ASSERT(e.event.ident == (unsigned)fd);

      if (events == READ) {
        e.fiber.reset();
        e.dg = NULL;
        if (e.fiberClose || e.dgClose) {
          return;
        }
      } else if (events == CLOSE) {
        e.fiberClose.reset();
        e.dgClose = NULL;
        if (e.fiber || e.dg) {
          return;
        }
      }

      e.event.flags = EV_DELETE;
      int rc = kevent(kqfd, &e.event, 1, NULL, 0, NULL);

      if (rc) {
        LOG(ERROR) << this << " kevent(" << kqfd << ", (" << fd << ", " << eventsKey << ", EV_DELETE)): " << rc;
        throw std::current_exception();
      } else {
        LOG(INFO) << this << " kevent(" << kqfd << ", (" << fd << ", " << eventsKey << ", EV_DELETE)): " << rc;
      }

      pendingEvents.erase(it);
    }

    bool IOManager::stopping(uint64 *nextTimeout) {
      *nextTimeout = nextTimer();
      if (*nextTimeout == ~0ul && Scheduler::Stopping()) {
        absl::MutexLock lock(&mutex);
        if (pendingEvents.empty()) {
          return true;
        }
      }
      return false;
    }

    void IOManager::idle() {
      struct kevent events[64];

      while (true) {
        uint64 nextTimeout;
        if (stopping(&nextTimeout)) {
          return;
        }
        int rc;

        do {
          struct timespec *timeout = NULL, timeoutStorage;
          if (nextTimeout != ~0ull) {
            timeout = &timeoutStorage;
            timeout->tv_sec = nextTimeout / 1000000;
            timeout->tv_nsec = (nextTimeout % 1000000) * 1000;
          }
          rc = kevent(kqfd, NULL, 0, events, 64, timeout);
          if (rc < 0 && errno == EINTR) {
            nextTimeout = nextTimer();
          } else {
            break;
          }
        } while (true);

        if (rc < 0) {
          LOG(ERROR) << this << " kevent(" << kqfd << "): " << rc;
          throw std::current_exception();
        } else {
          LOG(INFO) << this << " kevent(" << kqfd << "): " << rc;
        }
        std::vector<std::function<void()>> expired = processTimers();
        if (!expired.empty()) {
          schedule(expired.begin(), expired.end());
          expired.clear();
        }

        std::exception_ptr exception;
        for (int i = 0; i < rc; ++i) {
          struct kevent &event = events[i];
          if (static_cast<int>(event.ident) == tickleFds[0]) {
            unsigned char dummy;
            SPAN_ASSERT(read(tickleFds[0], &dummy, 1) == 1);
            LOG(INFO) << this << " received tickle (" << event.data << " remaining)";
            continue;
          }

          absl::MutexLock lock(&mutex);
          Event key;
          switch (event.filter) {
            case EVFILT_READ:
              key = READ;
              break;
            case EVFILT_WRITE:
              key = WRITE;
              break;
            default:
              SPAN_ASSERT(false);  // NOT REACHED
          }

          std::map<std::pair<int, Event>, AsyncEvent>::iterator it = pendingEvents.find(
            std::pair<int, Event>(static_cast<int>(event.ident), key));
          if (it == pendingEvents.end()) {
            continue;
          }
          AsyncEvent &e = it->second;

          bool remove = false;
          bool eof = !!(event.flags & EV_EOF);
          if ( (event.flags & EV_EOF) || (!e.dgClose && !e.fiberClose) ) {
            remove = true;
            event.flags = EV_DELETE;

            int rc2 = kevent(kqfd, &event, 1, NULL, 0, NULL);
            if (rc2) {
              LOG(ERROR) << this << " kevent(" << kqfd << ", (" << event.ident << ", "
                << event.filter << ", EV_DELETE)): " << rc2;
              exception = std::current_exception();
              continue;
            } else {
              LOG(INFO) << this << " kevent(" << kqfd << ", (" << event.ident << ", "
                << event.filter << ", EV_DELETE)): " << rc2;
            }
          }

          if (e.dg) {
            e.scheduler->schedule(&e.dg);
          } else if (e.fiber) {
            e.scheduler->schedule(&e.fiber);
          }

          if (eof && e.event.filter == EVFILT_READ) {
            if (e.dgClose) {
              e.schedulerClose->schedule(&e.dgClose);
            } else if (e.fiberClose) {
              e.schedulerClose->schedule(&e.fiberClose);
            }
          }

          if (remove) {
            pendingEvents.erase(it);
          }
        }

        if (exception) {
          throw exception;
        }

        try {
          fibers::Fiber::yield();
        } catch (...) {
          return;
        }
      }
    }

    void IOManager::tickle() {
      int rc = write(tickleFds[1], "T", 1);
      LOG(INFO) << this << " write(" << tickleFds[1] << ", 1): " << rc;
      SPAN_ASSERT(rc ==  1);
    }
  }  // namespace io
}  // namespace span

#endif
