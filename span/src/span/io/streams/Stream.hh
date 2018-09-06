#ifndef SPAN_SRC_SPAN_IO_STREAMS_STREAM_HH_
#define SPAN_SRC_SPAN_IO_STREAMS_STREAM_HH_

#include <stddef.h>
#include <string_view>

#include <memory>
#include <string>

#include "span/Common.hh"
#include "span/third_party/slimsig/slimsig.hh"

namespace span {
  namespace io {
    namespace streams {
      struct Buffer;

      class Stream {
      public:
        typedef std::shared_ptr<Stream> ptr;

        enum CloseType {
          NONE  = 0x00,
          READ  = 0x01,
          WRITE = 0x02,
          BOTH  = 0x03,
        };

        enum Anchor {
          BEGIN,
          CURRENT,
          END
        };

        Stream() = default;

        virtual ~Stream() noexcept(false) {}

        virtual bool supportsHalfClose() { return false; }
        virtual bool supportsRead() { return false; }
        virtual bool supportsWrite() { return false; }
        virtual bool supportsSeek() { return false; }

        virtual bool supportsTell() { return supportsSeek(); }
        virtual bool supportsSize() { return false; }
        virtual bool supportsTruncate() { return false; }
        virtual bool supportsFind() { return false; }
        virtual bool supportsUnread() { return false; }

        virtual void close(CloseType type = BOTH) {}

        virtual size_t read(Buffer *buffer, size_t len);
        virtual size_t read(void *buffer, size_t len);
        virtual void cancelRead() {}

        virtual size_t write(const Buffer *buffer, size_t len);
        virtual size_t write(const void *buffer, size_t len);
        size_t write(const char *string);
        virtual void cancelWrite() {}

        virtual int64 seek(int64 offset, Anchor anchor = BEGIN);
        int64 tell() { return seek(0, CURRENT); }

        virtual int64 size();

        virtual void truncate(int64 size);

        virtual void flush(bool flushParent = true) {}

        virtual ptrdiff_t find(char delimiter, size_t sanitySize = ~0, bool throwIfNotFound = true);
        virtual ptrdiff_t find(const std::string_view, size_t sanitySize = ~0, bool throwIfNotFound = true);

        std::string getDelimited(char delimiter = '\n', bool eofIsDelimiter = false, bool includeDelimiter = true);
        std::string getDelimited(const std::string_view delimiter, bool eofIsDelimiter = false,
          bool includeDelimiter = true);

        virtual void unread(const Buffer *buffer, size_t length);

        virtual slimsig::signal_t<void()>::connection onRemoteClose(const std::function<void()> slot) {
          return slimsig::signal_t<void()>::connection();
        }

      protected:
        size_t read(Buffer *buffer, size_t len, bool coalesce);
        size_t write(const Buffer *buffer, size_t len, bool coalesce);

      private:
        Stream(const Stream& rhs) = delete;
      };
    }  // namespace streams
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_STREAMS_STREAM_HH_
