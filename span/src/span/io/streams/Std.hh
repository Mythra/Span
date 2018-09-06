#ifndef SPAN_SRC_SPAN_IO_STREAMS_STD_HH_
#define SPAN_SRC_SPAN_IO_STREAMS_STD_HH_

#include "span/Common.hh"

#if PLATFORM != PLATFORM_WIN32
#include "span/io/streams/Fd.hh"
#endif

namespace span {
  namespace io {
    namespace streams {
      class StdStream : public NativeStream {
      protected:
        StdStream(span::io::IOManager *ioManager, span::fibers::Scheduler *scheduler, int stream);
      };

      class StdinStream : public StdStream {
      public:
#if PLATFORM != PLATFORM_WIN32
        StdinStream() : StdStream(NULL, NULL, STDIN_FILENO) {
        }
        explicit StdinStream(span::io::IOManager *manager) : StdStream(manager, NULL, STDIN_FILENO) {
        }
        explicit StdinStream(span::fibers::Scheduler *scheduler) : StdStream(NULL, scheduler, STDIN_FILENO) {
        }
        StdinStream(span::io::IOManager *manager, span::fibers::Scheduler *scheduler) :
          StdStream(manager, scheduler, STDIN_FILENO) {
        }

        bool supportsWrite() { return false; }
#endif
      };

      class StdoutStream : public StdStream {
      public:
#if PLATFORM != PLATFORM_WIN32
        StdoutStream() : StdStream(NULL, NULL, STDOUT_FILENO) {
        }
        explicit StdoutStream(span::io::IOManager *manager) : StdStream(manager, NULL, STDOUT_FILENO) {
        }
        explicit StdoutStream(span::fibers::Scheduler *scheduler) : StdStream(NULL, scheduler, STDOUT_FILENO) {
        }
        StdoutStream(span::io::IOManager *manager, span::fibers::Scheduler *scheduler) :
          StdStream(manager, scheduler, STDOUT_FILENO) {
        }

        bool supportsRead() { return false; }
#endif
      };

      class StderrStream : public StdStream {
      public:
#if PLATFORM != PLATFORM_WIN32
        StderrStream() : StdStream(NULL, NULL, STDERR_FILENO) {
        }
        explicit StderrStream(span::io::IOManager *manager) : StdStream(manager, NULL, STDERR_FILENO) {
        }
        explicit StderrStream(span::fibers::Scheduler *scheduler) : StdStream(NULL, scheduler, STDERR_FILENO) {
        }
        StderrStream(span::io::IOManager *manager, span::fibers::Scheduler *scheduler) :
          StdStream(manager, scheduler, STDERR_FILENO) {
        }

        bool supportsRead() { return false; }
#endif
      };
    }  // namespace streams
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_STREAMS_STD_HH_
