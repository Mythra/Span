#ifndef SPAN_SRC_SPAN_IO_STREAMS_NULL_HH_
#define SPAN_SRC_SPAN_IO_STREAMS_NULL_HH_

#include "span/Common.hh"
#include "span/io/streams/Buffer.hh"
#include "span/io/streams/Stream.hh"

namespace span {
  namespace io {
    namespace streams {
      class NullStream : public Stream {
      public:
        static Stream::ptr get_ptr() { return Stream::ptr(&nullStream_, &nop<Stream *>); }

        bool supportsRead() { return true; }
        bool supportsWrite() { return true; }
        bool supportsSeek() { return true; }
        bool supportsSize() { return true; }

        size_t read(Buffer *buff, size_t len) { return 0; }
        size_t read(void *buff, size_t len) { return 0; }
        size_t write(const Buffer *buff, size_t len) { return len; }
        size_t write(const void *buff, size_t len) { return len; }
        int64 seek(int64 offset, Anchor anchor = BEGIN) { return 0; }
        int64 size() { return 0; }

      private:
        NullStream() {}

        static NullStream nullStream_;
      };
    }  // namespace streams
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_STREAMS_NULL_HH_
