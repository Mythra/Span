#include "span/io/streams/Fd.hh"

#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <algorithm>
#include <limits>
#include <vector>

#include "span/Common.hh"
#include "span/io/IOManager.hh"
#include "span/io/streams/Buffer.hh"
#include "span/exceptions/Assert.hh"

#include "glog/logging.h"

namespace span {
  namespace io {
    namespace streams {
      FDStream::FDStream() : ioManager_(NULL), scheduler_(NULL), fd_(-1), own_(false), cancelledRead_(false),
        cancelledWrite_(false) {}

      void FDStream::init(int fd, ::span::io::IOManager *ioManager, ::span::fibers::Scheduler *scheduler, bool own) {
        SPAN_ASSERT(fd >= 0);
        ioManager_ = ioManager;
        scheduler_ = scheduler;
        fd_ = fd;
        own_ = own;
        cancelledRead_ = cancelledWrite_ = false;

        if (ioManager_) {
          if (fcntl(fd_, F_SETFL, O_NONBLOCK)) {
            if (own) {
              ::close(fd_);
              fd_ = -1;
            }
            throw std::runtime_error("fcntl");
          }
        }
      }

      FDStream::~FDStream() {
        if (own_ && fd_ >= 0) {
          ::span::fibers::SchedulerSwitcher switcher(scheduler_);
          int rc = ::close(fd_);
          if (rc) {
            LOG(ERROR) << this << " close(" << fd_ << "): " << rc << "(" << lastError() << ")";
          } else {
            DLOG(INFO) << this << " close(" << fd_ << "): " << rc << "(" << lastError() << ")";
          }
        }
      }

      void FDStream::close(CloseType type) {
        SPAN_ASSERT(type == BOTH);

        if (fd_ > 0 && own_) {
          ::span::fibers::SchedulerSwitcher switcher(scheduler_);
          int rc = ::close(fd_);
          error_t error = lastError();
          if (rc) {
            LOG(ERROR) << this << " close(" << fd_ << "): " << rc << "(" << error << ")";
            throw std::runtime_error("close");
          }
          DLOG(INFO) << this << " close(" << fd_ << "): " << rc << "(" << error << ")";
          fd_ = -1;
        }
      }

      size_t FDStream::read(Buffer *buff, size_t len) {
        if (ioManager_ && cancelledRead_) {
          throw std::runtime_error("Operation aborted exception!");
        }
        ::span::fibers::SchedulerSwitcher switcher(ioManager_ ? NULL : scheduler_);
        SPAN_ASSERT(fd_ >= 0);
        if (len > 0xFFFFFFFE) {
          len = 0xFFFFFFFE;
        }
        std::vector<iovec> iovs = buff->writeBuffers(len);
        int rc = readv(fd_, &iovs[0], iovs.size());
        while (rc < 0 && lastError() == EAGAIN && ioManager_) {
          DLOG(INFO) << this << " readv(" << fd_ << ", " << len << "): " << rc << " (EGAIN)";
          ioManager_->registerEvent(fd_, IOManager::READ);
          ::span::fibers::Scheduler::yieldTo();
          if (cancelledRead_) {
            throw std::runtime_error("Operation aborted exception");
          }
          rc = readv(fd_, &iovs[0], iovs.size());
        }
        error_t error = lastError();
        if (rc < 0) {
          LOG(ERROR) << this << " readv(" << fd_ << ", " << len << "): " << rc << " (" << error << ")";
          throw std::runtime_error("readv");
        }
        buff->produce(rc);
        return rc;
      }

      size_t FDStream::read(void *buff, size_t len) {
        if (ioManager_ && cancelledRead_) {
          throw std::runtime_error("Operation aborted exception!");
        }
        ::span::fibers::SchedulerSwitcher switcher(ioManager_ ? NULL : scheduler_);
        SPAN_ASSERT(fd_ >= 0);
        if (len >= 0xFFFFFFFE) {
          len = 0xFFFFFFFE;
        }
        int rc = ::read(fd_, buff, len);
        while (rc < 0 && lastError() == EAGAIN && ioManager_) {
          DLOG(INFO) << this << " read(" << fd_ << ", " << len << "): " << rc << " (EAGAIN)";
          ioManager_->registerEvent(fd_, IOManager::READ);
          ::span::fibers::Scheduler::yieldTo();
          if (cancelledRead_) {
            throw std::runtime_error("Operation Aborted Exception");
          }
          rc = ::read(fd_, buff, len);
        }
        error_t error = lastError();
        if (rc < 0) {
          LOG(ERROR) << this << " read(" << fd_ << ", " << len << "): " << rc << " (" << error << ")";
          throw std::runtime_error("read");
        }
        DLOG(INFO) << this << " read(" << fd_ << ", " << len << "): " << rc << " (" << error << ")";
        return rc;
      }

      void FDStream::cancelRead() {
        cancelledRead_ = true;
        if (ioManager_) {
          ioManager_->cancelEvent(fd_, IOManager::WRITE);
        }
      }

      size_t FDStream::write(const Buffer *buff, size_t len) {
        if (ioManager_ && cancelledWrite_) {
          throw std::runtime_error("Operation aborted exception");
        }
        ::span::fibers::SchedulerSwitcher switcher(ioManager_ ? NULL : scheduler_);
        SPAN_ASSERT(fd_ >= 0);
        len = std::min(len, static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
        const std::vector<iovec> iovs = buff->readBuffers(len);
        ssize_t rc = 0;
        const int count = std::min(iovs.size(), static_cast<size_t>(IOV_MAX));

        while ((rc = writev(fd_, &iovs[0], count)) < 0 && lastError() == EAGAIN && ioManager_) {
          DLOG(INFO) << this << " writev(" << fd_ << ", " << len << "): " << rc << " (EAGAIN)";
          ioManager_->registerEvent(fd_, IOManager::WRITE);
          ::span::fibers::Scheduler::yieldTo();
          if (cancelledWrite_) {
            throw std::runtime_error("Operation aborted exception");
          }
          rc = writev(fd_, &iovs[0], iovs.size());
        }
        error_t error = lastError();

        if (rc < 0) {
          LOG(ERROR) << this << " writev(" << fd_ << ", " << len << "): " << rc << " (" << error << ")";
          throw std::runtime_error("writev");
        } else if (rc == 0) {
          throw std::runtime_error("Zero length write");
        }
        DLOG(INFO) << this << " writev(" << fd_ << ", " << len << "): " << rc << " (" << error << ")";
        return rc;
      }

      size_t FDStream::write(const void *buff, size_t len) {
        if (ioManager_ && cancelledWrite_) {
          throw std::runtime_error("Operation aborted exception");
        }
        ::span::fibers::SchedulerSwitcher switcher(ioManager_ ? NULL : scheduler_);
        SPAN_ASSERT(fd_ >= 0);

        if (len > 0xFFFFFFFE) {
          len = 0xFFFFFFFE;
        }

        int rc = ::write(fd_, buff, len);
        while (rc < 0 && lastError() == EAGAIN && ioManager_) {
          DLOG(INFO) << this << " write(" << fd_ << ", " << len << "): " << rc << " (EAGAIN)";
          ioManager_->registerEvent(fd_, IOManager::WRITE);
          ::span::fibers::Scheduler::yieldTo();
          if (cancelledWrite_) {
            throw std::runtime_error("Operation aborted exception!");
          }
          rc = ::write(fd_, buff, len);
        }

        error_t error = lastError();
        if (rc < 0) {
          LOG(ERROR) << this << " write(" << fd_ << ", " << len << "): " << rc << " (" << error << ")";
          throw std::runtime_error("write");
        } else if (rc == 0) {
          throw std::runtime_error("Zero length write!");
        }
        DLOG(INFO) << this << " write(" << fd_ << ", " << len << "): " << rc << " (" << error << ")";
        return rc;
      }

      void FDStream::cancelWrite() {
        cancelledWrite_ = true;
        if (ioManager_) {
          ioManager_->cancelEvent(fd_, IOManager::WRITE);
        }
      }

      int64 FDStream::seek(int64 offset, Anchor anchor) {
        ::span::fibers::SchedulerSwitcher switcher(scheduler_);
        SPAN_ASSERT(fd_ >= 0);
        int64 pos = lseek(fd_, offset, static_cast<int>(anchor));
        error_t error = lastError();

        if (pos < 0) {
          LOG(ERROR) << this << " lseek(" << fd_ << ", " << offset << ", " << anchor << "): " << pos << " (" << error
            << ")";
          throw std::runtime_error("lseek");
        }
        DLOG(INFO) << this << " lseek(" << fd_ << ", " << offset << ", " << anchor << "): " << pos << " (" << error
          << ")";

        return pos;
      }

      int64 FDStream::size() {
        ::span::fibers::SchedulerSwitcher switcher(scheduler_);
        SPAN_ASSERT(fd_ >= 0);

        struct stat statbuf;
        int rc = fstat(fd_, &statbuf);
        error_t error = lastError();

        if (rc) {
          LOG(ERROR) << this << " fstat(" << fd_ << "): " << rc << " (" << error << ")";
          throw std::runtime_error("fstat");
        }
        DLOG(INFO) << this << " fstat(" << fd_ << "): " << rc << " (" << error << ")";
        return statbuf.st_size;
      }

      void FDStream::truncate(int64 size) {
        ::span::fibers::SchedulerSwitcher switcher(scheduler_);
        SPAN_ASSERT(fd_ >= 0);

        int rc = ftruncate(fd_, size);
        error_t error = lastError();
        if (rc) {
          LOG(ERROR) << this << " ftruncate(" << fd_ << ", " << size << "): " << rc << " (" << error << ")";
          throw std::runtime_error("ftruncate");
        }
        DLOG(INFO) << this << " ftruncate(" << fd_ << ", " << size << "): " << rc << " (" << error << ")";
      }

      void FDStream::flush(bool flushParent) {
        ::span::fibers::SchedulerSwitcher switcher(scheduler_);
        SPAN_ASSERT(fd_ >= 0);
        int rc = fsync(fd_);
        error_t error = lastError();

        if (rc) {
          LOG(ERROR) << this << " fsync(" << fd_ << "): " << rc << " (" << error << ")";
          throw std::runtime_error("fsync");
        }
        DLOG(INFO) << this << " fsync(" << fd_ << "): " << rc << " (" << error << ")";
      }
    }  // namespace streams
  }  // namespace io
}  // namespace span
