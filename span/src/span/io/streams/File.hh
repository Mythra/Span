#ifndef SPAN_SRC_SPAN_IO_STREAMS_FILE_HH_
#define SPAN_SRC_SPAN_IO_STREAMS_FILE_HH_

#include <memory>
#include <string_view>

#include "span/Common.hh"
#include "span/io/IOManager.hh"
#include "span/fibers/Scheduler.hh"

#if PLATFORM != PLATFORM_WIN32
#include "span/io/streams/Fd.hh"

#include <fcntl.h>
#endif

namespace span {
  namespace io {
    namespace streams {
      class FileStream : public NativeStream {
      public:
        typedef std::shared_ptr<FileStream> ptr;

#if PLATFORM != PLATFORM_WIN32
        enum AccessFlags {
          READ      = O_RDONLY,
          WRITE     = O_WRONLY,
          READWRITE = O_RDWR,
          APPEND    = O_APPEND | O_WRONLY,
        };
        enum CreateFlags {
          // Open a file fail if does not exist.
          OPEN = 1,
          // Create a file. Fail if it exists.
          CREATE,
          // Open a file. Create it if it does not exist.
          OPEN_OR_CREATE,
          // Open a file, and recreate it from scratch. Fail if it does not exist.
          OVERWRITE,
          // Create a file. If it exists recreate it from scratch.
          OVERWRITE_OR_CREATE,
          // Delete the file when it is closed. Can be combined with any of the other options.
          DELETE_ON_CLOSE = 0x80000000
        };
#endif

        FileStream(const std::string_view path, AccessFlags accessFlags = READWRITE, CreateFlags createFlags = OPEN,
          span::io::IOManager *ioManager = NULL, span::fibers::Scheduler *scheduler = NULL) {
          init(path, accessFlags, createFlags, ioManager, scheduler);
        }

        bool supportsRead() { return supportsRead_ && NativeStream::supportsRead(); }
        bool supportsWrite() { return supportsWrite_ && NativeStream::supportsWrite(); }
        bool supportsSeek() { return supportsSeek_ && NativeStream::supportsSeek(); }

        std::string_view path() const { return path_; }

      protected:
        FileStream();
        void init(const std::string_view path, AccessFlags accessFlags = READWRITE, CreateFlags createFlags = OPEN,
          span::io::IOManager *ioManager = NULL, span::fibers::Scheduler *scheduler = NULL);

      private:
        bool supportsRead_, supportsWrite_, supportsSeek_;
        std::string_view path_;
      };
    }  // namespace streams
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_STREAMS_FILE_HH_
