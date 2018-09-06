#include "span/io/streams/Std.hh"

#include "span/Common.hh"
#include "span/io/IOManager.hh"
#include "span/fibers/Scheduler.hh"

namespace span {
  namespace io {
    namespace streams {
      StdStream::StdStream(span::io::IOManager *ioManager, span::fibers::Scheduler *scheduler, int stream) {
#if PLATFORM != PLATFORM_WIN32
        init(stream, ioManager, scheduler, stream);
#endif
      }
    }  // namespace streams
  }  // namespace io
}  // namespace span
