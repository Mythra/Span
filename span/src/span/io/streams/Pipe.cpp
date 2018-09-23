#include "span/io/streams/Pipe.hh"

#include <algorithm>
#include <memory>
#include <utility>

#include "span/Common.hh"
#include "span/exceptions/Assert.hh"
#include "span/fibers/Fiber.hh"
#include "span/fibers/Scheduler.hh"
#include "span/io/streams/Buffer.hh"
#include "span/io/streams/File.hh"
#include "span/io/streams/Stream.hh"
#include "span/third_party/slimsig/slimsig.hh"

#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
#include <crt_externs.h>
#endif

#include "absl/synchronization/mutex.h"

namespace span {
  namespace io {
    namespace streams {
      class PipeStream : public Stream {
        friend std::pair<Stream::ptr, Stream::ptr> pipeStream(size_t);

      public:
        typedef std::shared_ptr<PipeStream> ptr;
        typedef std::weak_ptr<PipeStream> weak_ptr;

        explicit PipeStream(size_t buffSize);
        ~PipeStream();

        bool supportsHalfClose() { return true; }
        bool supportsRead() { return true; }
        bool supportsWrite() { return true; }

        void close(CloseType type = BOTH);
        using Stream::read;
        size_t read(Buffer *buff, size_t len);
        void cancelRead();
        using Stream::write;
        size_t write(const Buffer *buff, size_t len);
        void cancelWrite();
        void flush(bool flushParent = true);

        slimsig::signal_t<void()>::connection onRemoteClose(const std::function<void()> dg);

      private:
        PipeStream::weak_ptr otherStream_;
        std::shared_ptr<absl::Mutex> mutex_;
        Buffer readBuff_;
        size_t buffSize_;
        bool cancelledRead_, cancelledWrite_;
        CloseType closed_, otherClosed_;
        span::fibers::Scheduler *pendingWriterScheduler, *pendingReaderScheduler;
        std::shared_ptr<span::fibers::Fiber> pendingWriter_, pendingReader_;
        slimsig::signal_t<void()> onRemoteClose_;
      };

      PipeStream::PipeStream(size_t buffSize) : buffSize_(buffSize),
        cancelledRead_(false), cancelledWrite_(false), closed_(NONE),
        otherClosed_(NONE), pendingWriterScheduler(NULL),
        pendingReaderScheduler(NULL) {}

      PipeStream::~PipeStream() {
        DLOG(INFO) << this << " destructing";
        PipeStream::ptr otherStream = otherStream_.lock();
        absl::MutexLock _lock(mutex_.get());
        if (otherStream) {
          SPAN_ASSERT(!otherStream->pendingReader_);
          SPAN_ASSERT(!otherStream->pendingReaderScheduler);
          SPAN_ASSERT(!otherStream->pendingWriter_);
          SPAN_ASSERT(!otherStream->pendingWriterScheduler);

          if (!readBuff_.readAvailable()) {
            otherStream->otherClosed_ = static_cast<CloseType>(otherStream->otherClosed_ | READ);
          } else {
            otherStream->otherClosed_ = static_cast<CloseType>(otherStream->otherClosed_ & ~READ);
          }
          otherStream->onRemoteClose_.emit();
        }
        if (pendingReader_) {
          SPAN_ASSERT(pendingReaderScheduler);
          DLOG(INFO) << otherStream << " scheduling read";
          pendingReaderScheduler->schedule(pendingReader_);
          pendingReader_.reset();
          pendingReaderScheduler = NULL;
        }
        if (pendingWriter_) {
          SPAN_ASSERT(pendingWriterScheduler);
          DLOG(INFO) << otherStream << " scheduling write";
          pendingWriterScheduler->schedule(pendingWriter_);
          pendingWriter_.reset();
          pendingWriterScheduler = NULL;
        }
      }

      void PipeStream::close(CloseType type) {
        PipeStream::ptr otherStream = otherStream_.lock();
        absl::MutexLock _lock(mutex_.get());
        bool closeWriteFirstTime = !(closed_ & WRITE) && (type & WRITE);
        closed_ = static_cast<CloseType>(closed_ | type);
        if (otherStream) {
          otherStream->otherClosed_ = closed_;
          if (closeWriteFirstTime) {
            otherStream->onRemoteClose_.emit();
          }
        }
        if (pendingReader_ && (closed_ & WRITE)) {
          SPAN_ASSERT(pendingReaderScheduler);
          DLOG(INFO) << otherStream << " scheduling read";
          pendingReaderScheduler->schedule(pendingReader_);
          pendingReader_.reset();
          pendingReaderScheduler = NULL;
        }
        if (pendingWriter_ && (closed_ & READ)) {
          SPAN_ASSERT(pendingWriterScheduler);
          DLOG(INFO) << otherStream << " shceduling write";
          pendingWriterScheduler->schedule(pendingWriter_);
          pendingWriter_.reset();
          pendingWriterScheduler = NULL;
        }
      }

      size_t PipeStream::read(Buffer *buff, size_t len) {
        SPAN_ASSERT(len != 0);

        while (true) {
          {
            PipeStream::ptr otherStream = otherStream_.lock();
            absl::MutexLock _lock(mutex_.get());
            if (closed_ & READ) {
              throw std::runtime_error("Broken Pipe!");
            }
            if (!otherStream && !(otherClosed_ & WRITE)) {
              throw std::runtime_error("Broken Pipe!");
            }
            size_t avail = readBuff_.readAvailable();
            if (avail > 0) {
              size_t todo = std::min<size_t>(len, avail);
              buff->copyIn(&readBuff_, todo);
              readBuff_.consume(todo);
              if (pendingWriter_) {
                SPAN_ASSERT(pendingWriterScheduler);
                DLOG(INFO) << otherStream << " scheduling write";
                pendingWriterScheduler->schedule(pendingWriter_);
                pendingWriter_.reset();
                pendingWriterScheduler = NULL;
              }
              DLOG(INFO) << this << " read(" << len << "): " << todo;
              return todo;
            }

            if (otherClosed_ & WRITE) {
              DLOG(INFO) << this << " read(" << len << "): " << 0;
              return 0;
            }

            if (cancelledRead_) {
              throw std::runtime_error("Aborted");
            }

            // Wait for the other stream to schedule us;
            SPAN_ASSERT(!otherStream->pendingReader_);
            SPAN_ASSERT(!otherStream->pendingReaderScheduler);
            DLOG(INFO) << this << " waiting to read";
            otherStream->pendingReader_ = span::fibers::Fiber::getThis();
            otherStream->pendingReaderScheduler = span::fibers::Scheduler::getThis();
          }

          try {
            span::fibers::Scheduler::yieldTo();
          } catch (...) {
            PipeStream::ptr otherStream = otherStream_.lock();
            absl::MutexLock _lock(mutex_.get());
            if (otherStream && otherStream->pendingReader_ == span::fibers::Fiber::getThis()) {
              SPAN_ASSERT(otherStream->pendingReaderScheduler == span::fibers::Scheduler::getThis());
              otherStream->pendingReader_.reset();
              otherStream->pendingReaderScheduler = NULL;
            }
            throw;
          }
        }
      }

      void PipeStream::cancelRead() {
        PipeStream::ptr otherStream = otherStream_.lock();
        absl::MutexLock _lock(mutex_.get());
        cancelledRead_ = true;
        if (otherStream && otherStream->pendingReader_) {
          SPAN_ASSERT(otherStream->pendingReaderScheduler);
          DLOG(INFO) << this << " cancelling read";
          otherStream->pendingReaderScheduler->schedule(otherStream->pendingReader_);
          otherStream->pendingReader_.reset();
          otherStream->pendingReaderScheduler = NULL;
        }
      }

      size_t PipeStream::write(const Buffer *buff, size_t len) {
        SPAN_ASSERT(len != 0);

        while (true) {
          {
            PipeStream::ptr otherStream = otherStream_.lock();
            absl::MutexLock _lock(mutex_.get());
            if (closed_ & WRITE) {
              throw std::runtime_error("Broken Pipe");
            }
            if (!otherStream || (otherStream->closed_ & READ)) {
              throw std::runtime_error("Broken Pipe");
            }

            size_t available = otherStream->readBuff_.readAvailable();
            size_t todo = std::min<size_t>(buffSize_ - available, len);
            if (todo != 0) {
              otherStream->readBuff_.copyIn(buff, todo);
              if (pendingReader_) {
                SPAN_ASSERT(pendingReaderScheduler);
                DLOG(INFO) << otherStream << " scheduling read";
                pendingReaderScheduler->schedule(pendingReader_);
                pendingReader_.reset();
                pendingReaderScheduler = NULL;
              }
              DLOG(INFO) << this << " write(" << len << "): " << todo;
              return todo;
            }

            if (cancelledWrite_) {
              throw std::runtime_error("Operation aborted");
            }

            // Wait for other stream to schedule us.
            SPAN_ASSERT(!otherStream->pendingWriter_);
            SPAN_ASSERT(!otherStream->pendingWriterScheduler);
            DLOG(INFO) << this << " waiting to write";
            otherStream->pendingWriter_ = span::fibers::Fiber::getThis();
            otherStream->pendingWriterScheduler = span::fibers::Scheduler::getThis();
          }

          try {
            span::fibers::Scheduler::yieldTo();
          } catch (...) {
            PipeStream::ptr otherStream = otherStream_.lock();
            absl::MutexLock _lock(mutex_.get());
            if (otherStream && otherStream->pendingWriter_ == span::fibers::Fiber::getThis()) {
              SPAN_ASSERT(otherStream->pendingWriterScheduler = span::fibers::Scheduler::getThis());
              otherStream->pendingWriter_.reset();
              otherStream->pendingWriterScheduler = NULL;
            }
            throw;
          }
        }
      }

      void PipeStream::cancelWrite() {
        PipeStream::ptr otherStream = otherStream_.lock();
        absl::MutexLock _lock(mutex_.get());
        cancelledWrite_ = true;
        if (otherStream && otherStream->pendingWriter_) {
          SPAN_ASSERT(otherStream->pendingWriterScheduler);
          DLOG(INFO) << this << " cancelling write";
          otherStream->pendingWriterScheduler->schedule(otherStream->pendingWriter_);
          otherStream->pendingWriter_.reset();
          otherStream->pendingWriterScheduler = NULL;
        }
      }

      void PipeStream::flush(bool flushParent) {
        while (true) {
          {
            PipeStream::ptr otherStream = otherStream_.lock();
            absl::MutexLock _lock(mutex_.get());
            if (cancelledWrite_) {
              throw std::runtime_error("Operation Aborted");
            }
            if (!otherStream) {
              // See if they read everything before destructing.
              if (otherClosed_ & READ) {
                return;
              }
              throw std::runtime_error("Broken Pipe");
            }

            if (otherStream->readBuff_.readAvailable() == 0) {
              return;
            }
            if (otherStream->closed_ & READ) {
              throw std::runtime_error("Broken Pipe");
            }

            // Wait for other stream to schedule us.
            SPAN_ASSERT(!otherStream->pendingWriter_);
            SPAN_ASSERT(!otherStream->pendingWriterScheduler);
            DLOG(INFO) << this << " waiting to flush";
            otherStream->pendingWriter_ = span::fibers::Fiber::getThis();
            otherStream->pendingWriterScheduler = span::fibers::Scheduler::getThis();
          }

          try {
            span::fibers::Scheduler::yieldTo();
          } catch (...) {
            PipeStream::ptr otherStream = otherStream_.lock();
            absl::MutexLock _lock(mutex_.get());
            if (otherStream && otherStream->pendingWriter_ == span::fibers::Fiber::getThis()) {
              SPAN_ASSERT(otherStream->pendingWriterScheduler == span::fibers::Scheduler::getThis());
              otherStream->pendingWriter_.reset();
              otherStream->pendingWriterScheduler = NULL;
            }
            throw;
          }
        }
      }

      slimsig::signal_t<void()>::connection PipeStream::onRemoteClose(const std::function<void()> dg) {
        return onRemoteClose_.connect(dg);
      }

      std::pair<Stream::ptr, Stream::ptr> pipeStream(size_t buffSize) {
        if (buffSize == ~0u) {
          buffSize = 65536;
        }
        std::pair<PipeStream::ptr, PipeStream::ptr> result;
        result.first.reset(new PipeStream(buffSize));
        result.second.reset(new PipeStream(buffSize));
        DLOG(INFO) << "pipeStream(" << buffSize << "): {" << result.first << ", " << result.second << "}";
        result.first->otherStream_ = result.second;
        result.second->otherStream_ = result.first;
        result.first->mutex_.reset(new absl::Mutex());
        result.second->mutex_ = result.first->mutex_;
        return result;
      }

      std::pair<NativeStream::ptr, NativeStream::ptr> anonymousPipe(span::io::IOManager *ioManager) {
        std::pair<NativeStream::ptr, NativeStream::ptr> result;
#if PLATFORM != PLATFORM_WIN32
        int fds[2];
        if (pipe(fds)) {
          throw std::runtime_error("pipe");
        }
        try {
          result.first.reset(new FDStream(fds[0], ioManager));
          result.second.reset(new FDStream(fds[1], ioManager));
        } catch (...) {
          if (!result.first) {
            close(fds[0]);
          }
          if (!result.second) {
            close(fds[1]);
          }
          throw;
        }
#endif
        return result;
      }
    }  // namespace streams
  }  // namespace io
}  // namespace span
