#ifndef SPAN_SRC_SPAN_IO_STREAMS_PIPE_HH_
#define SPAN_SRC_SPAN_IO_STREAMS_PIPE_HH_

#include <utility>

#include "span/Common.hh"
#include "span/io/streams/Stream.hh"
#if PLATFORM != PLATFORM_WIN32
#include "span/io/streams/Fd.hh"
#endif

namespace span {
  namespace io {
    class IOManager;

    namespace streams {
      /**
       *  Create a user-space only, full-duplex anonymous pipe.
       */
      std::pair<Stream::ptr, Stream::ptr> pipeStream(size_t buffSize = ~0);

      /**
       * Create a kernel-level, half-duplex anonymous pipe.
       *
       * The streams created by this function will have a file handle, and are
       * suitable for usage with native OS APIs.
       */
      std::pair<NativeStream::ptr, NativeStream::ptr> anonymousPipe(IOManager *ioManager = NULL);
    }  // namespace streams
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_STREAMS_PIPE_HH_
