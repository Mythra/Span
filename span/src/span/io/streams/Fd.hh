#ifndef SPAN_SRC_SPAN_IO_STREAMS_FD_HH_
#define SPAN_SRC_SPAN_IO_STREAMS_FD_HH_

#include <memory>

#include "span/Common.hh"
#include "span/io/streams/Stream.hh"

namespace span {
  namespace fibers {
    class Scheduler;
  }  // namespace fibers

  namespace io {
    class IOManager;

    namespace streams {
      class FDStream : public Stream {
      public:
        typedef std::shared_ptr<FDStream> ptr;

        explicit FDStream(int fd, ::span::io::IOManager *ioManager = NULL, ::span::fibers::Scheduler *scheduler = NULL,
          bool own = true) {
          init(fd, ioManager, scheduler, own);
        }
        ~FDStream();

        bool supportsRead() { return true; }
        bool supportsWrite() { return true; }
        bool supportsSeek() { return true; }
        bool supportsSize() { return true; }
        bool supportsTruncate() { return true; }

        void close(CloseType type = BOTH);
        size_t read(Buffer *buff, size_t len);
        size_t read(void *buff, size_t len);
        void cancelRead();
        size_t write(const Buffer *buff, size_t len);
        size_t write(const void *buff, size_t len);
        void cancelWrite();
        int64 seek(int64 offset, Anchor anchor = BEGIN);
        int64 size();
        void truncate(int64 size);
        void flush(bool flushParent = true);

        int fd() { return fd_; }

      protected:
        FDStream();
        void init(int fd, ::span::io::IOManager *ioManager = NULL, ::span::fibers::Scheduler *scheduler = NULL,
          bool own = true);

      private:
        ::span::io::IOManager *ioManager_;
        ::span::fibers::Scheduler *scheduler_;
        int fd_;
        bool own_, cancelledRead_, cancelledWrite_;
      };

      typedef FDStream NativeStream;
      typedef int NativeHandle;
    }  // namespace streams
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_STREAMS_FD_HH_
