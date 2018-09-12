#ifndef SPAN_SRC_SPAN_IO_STREAMS_BUFFER_HH_
#define SPAN_SRC_SPAN_IO_STREAMS_BUFFER_HH_

#include <stddef.h>

#include <list>
#include <memory>
#include <string>
#include <vector>

#if __has_include(<string_view>)
#include <string_view>
using std::string_view;
#else
#include <experimental/string_view>
using std::experimental::string_view;
#endif

#include "span/io/Socket.hh"

namespace span {
  namespace io {
    namespace streams {
      struct Buffer {
      public:
        Buffer();
        explicit Buffer(const Buffer *copy);
        explicit Buffer(const string_view string);
        Buffer(const void *data, size_t len);

        Buffer &operator= (const Buffer *copy);

        size_t readAvailable() const;
        size_t writeAvailable() const;
        // Primarily for unit tests.
        size_t segments() const;

        void adopt(void *buffer, size_t len);
        void reserve(size_t len);
        void compact();
        void clear(bool clearWriteAvailableAsWell = true);
        void produce(size_t len);
        void consume(size_t len);
        void truncate(size_t len);

        const std::vector<iovec> readBuffers(size_t len = ~0) const;
        const iovec readBuffer(size_t len, bool reallocate) const;
        std::vector<iovec> writeBuffers(size_t len = ~0u);
        iovec writeBuffer(size_t len, bool reallocate);

        void copyIn(const Buffer *buf, size_t len = ~0, size_t pos = 0);
        void copyIn(const string_view string);
        void copyIn(const void *data, size_t len);

        void copyOut(Buffer *buffer, size_t len, size_t pos = 0) const {
          buffer->copyIn(this, len, pos);
        }
        void copyOut(void *buffer, size_t len, size_t pos = 0) const;

        ptrdiff_t find(char delimiter, size_t len = ~0) const;
        ptrdiff_t find(const string_view string, size_t len = ~0) const;

        std::string to_string() const;
        std::string getDelimited(char delimiter, bool eofIsDelimiter = true, bool includeDelimiter = true);
        std::string getDelimited(const string_view delimiter, bool eofIsDelimiter = true,
          bool includeDelimiter = true);
        void visit(std::function<void(const void *, size_t)> dg, size_t len = ~0) const;

        bool operator== (const Buffer &rhs) const;
        bool operator!= (const Buffer &rhs) const;
        bool operator== (const string_view str) const;
        bool operator!= (const string_view str) const;

      private:
        struct SegmentData {
          friend struct Buffer;

        public:
          SegmentData();
          explicit SegmentData(size_t len);
          SegmentData(void *buff, size_t len);

          SegmentData slice(size_t start, size_t len = ~0);
          const SegmentData slice(size_t start, size_t len = ~0) const;

          void extend(size_t len);

          void *start() { return start_; }
          const void *start() const { return start_; }
          size_t len() const { return len_; }

        private:
          void start(void *p) { start_ = p; }
          void len(size_t l) { len_ = l; }

          void *start_;
          size_t len_;
          std::shared_ptr<unsigned char> array_;
        };

        struct Segment {
          friend struct Buffer;

        public:
          explicit Segment(size_t len);
          explicit Segment(SegmentData);
          Segment(void *buff, size_t len);

          size_t readAvailable() const;
          size_t writeAvailable() const;
          size_t len() const;

          void produce(size_t len);
          void consume(size_t len);
          void truncate(size_t len);
          void extend(size_t len);

          const SegmentData readBuffer() const;
          const SegmentData writeBuffer() const;
          SegmentData writeBuffer();

        private:
          size_t writeIndex_;
          SegmentData data_;

          void invariant() const;
        };

        std::list<Segment> segments_;
        size_t readAvailable_;
        size_t writeAvailable_;
        std::list<Segment>::iterator writeIt_;

        int opCmp(const Buffer *rhs) const;
        int opCmp(string_view string, size_t len) const;

        void invariant() const;
      };
    }  // namespace streams
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_STREAMS_BUFFER_HH_
