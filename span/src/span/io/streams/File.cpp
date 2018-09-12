#include "span/io/streams/File.hh"

#if __has_include(<string_view>)
#include <string_view>
using std::string_view;
#else
#include <experimental/string_view>
using std::experimental::string_view;
#endif

#include "span/Common.hh"
#include "span/exceptions/Assert.hh"
#include "span/exceptions/Exception.hh"
#include "span/io/IOManager.hh"
#include "span/fibers/Scheduler.hh"

#include "glog/logging.h"

namespace span {
  namespace io {
    namespace streams {
      FileStream::FileStream() : supportsRead_(false), supportsWrite_(false), supportsSeek_(false) {
      }

      void FileStream::init(const string_view path, AccessFlags accessFlags, CreateFlags createFlags,
        span::io::IOManager *ioManager, span::fibers::Scheduler *scheduler) {
        NativeHandle handle;

#if PLATFORM != PLATFORM_WIN32
        int oflags = static_cast<int>(accessFlags);
        switch (createFlags & ~DELETE_ON_CLOSE) {
          case OPEN:
            break;
          case CREATE:
            oflags |= O_CREAT | O_EXCL;
            break;
          case OPEN_OR_CREATE:
            oflags |= O_CREAT;
            break;
          case OVERWRITE:
            oflags |= O_TRUNC;
            break;
          case OVERWRITE_OR_CREATE:
            oflags |= O_CREAT | O_TRUNC;
            break;
          default:
            throw std::runtime_error("not reached");
        }
        handle = open(path.data(), oflags, 0777);
        error_t error = lastError();

        DLOG(INFO) << "open(" << path.data() << ", " << oflags << "): " << handle << " (" << error << ")";
        if (handle < 0) {
          throw std::runtime_error("open");
        }
        if (createFlags & DELETE_ON_CLOSE) {
          int rc = unlink(path.data());
          if (rc != 0) {
            ::close(handle);
            throw std::runtime_error("unlink");
          }
        }
#endif
        NativeStream::init(handle, ioManager, scheduler);
        supportsRead_ = accessFlags == READ || accessFlags == READWRITE;
        supportsWrite_ = accessFlags == WRITE || accessFlags == READWRITE || accessFlags == APPEND;
        supportsSeek_ = accessFlags != APPEND;
        path_ = path;
      }
    }  // namespace streams
  }  // namespace io
}  // namespace span
