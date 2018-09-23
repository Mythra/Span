#ifndef SPAN_SRC_SPAN_IO_STREAMS_FILTER_HH_
#define SPAN_SRC_SPAN_IO_STREAMS_FILTER_HH_

#include <memory>

#if __has_include(<string_view>)
#include <string_view>
using std::string_view;
#else
#include <experimental/string_view>
using std::experimental::string_view;
#endif

#include "span/Common.hh"
#include "span/io/streams/Buffer.hh"
#include "span/io/streams/Stream.hh"
#include "span/third_party/slimsig/slimsig.hh"

namespace span {
  namespace io {
    namespace streams {
      /**
       * When inheriting from FilterStream, use parent()->xxx to call
       * method xxx on the parent stream.
       *
       * FilterStreams *must* implement one of the possible overloads for read()
       * and write(), even if it's just calling the parent version. Otherwise the
       * adapter functions in the base Stream will just call each other, and blow
       * the stack.
       */
      class FilterStream : public Stream {
      public:
        typedef std::shared_ptr<FilterStream> ptr;

        explicit FilterStream(Stream::ptr parent, bool own = true) : parent_(parent), own_(own) {}

        Stream::ptr parent() { return parent_; }
        void parent(Stream::ptr parent) { parent_ = parent; }
        bool ownsParent() { return own_; }
        void ownsParent(bool own) { own_ = own; }

        bool supportsHalfClose() { return parent_->supportsHalfClose(); }
        bool supportsRead() { return parent_->supportsRead(); }
        bool supportsWrite() { return parent_->supportsWrite(); }
        bool supportsSeek() { return parent_->supportsSeek(); }
        bool supportsTell() { return parent_->supportsTell(); }
        bool supportsSize() { return parent_->supportsSize(); }
        bool supportsTruncate() { return parent_->supportsTruncate(); }
        bool supportsFind() { return parent_->supportsFind(); }
        bool supportsUnread() { return parent_->supportsUnread(); }

        void close(CloseType type = BOTH) {
          if (own_) {
            parent_->close(type);
          }
        }
        void cancelRead() { parent_->cancelRead(); }
        void cancelWrite() { parent_->cancelWrite(); }

        int64 seek(int64 offset, Anchor anchor = BEGIN) {
          return parent_->seek(offset, anchor);
        }
        int64 size() {
          return parent_->size();
        }

        void truncate(int64 size) {
          parent_->truncate(size);
        }
        void flush(bool flushParent = true) {
          if (flushParent) {
            parent_->flush(true);
          }
        }

        ptrdiff_t find(char delim, size_t sanitySize = ~0, bool throwIfNotFound = true) {
          return parent_->find(delim, sanitySize, throwIfNotFound);
        }
        ptrdiff_t find(const string_view str, size_t sanitySize = ~0, bool throwIfNotFound = true) {
          return parent_->find(str, sanitySize, throwIfNotFound);
        }

        void unread(const Buffer *buff, size_t len) {
          return parent_->unread(buff, len);
        }

        slimsig::signal_t<void()>::connection onRemoteClose(const std::function<void()> dg) {
          return parent_->onRemoteClose(dg);
        }

      private:
        Stream::ptr parent_;
        bool own_;
      };

      /**
       * A mutating filter stream is one that declares that it changes the data
       * as it flows through it. It implicitly turns off and asserts features
       * that would need to be implemented by the inheritor, instead of
       * defaulting to the parent streams implementation.
       */
      class MutatingFilterStream : public FilterStream {
      public:
        bool supportsSeek() { return false; }
        bool supportsTell() { return supportsSeek(); }
        bool supportsSize() { return false; }
        bool supportsTruncate() { return false; }
        bool supportsFind() { return false; }
        bool supportsUnread() { return false; }

        int64 seek(int64 offset, Anchor anchor = BEGIN);
        int64 size();
        void truncate(int64 size);

        ptrdiff_t find(char delim, size_t sanitySize = ~0, bool throwIfNotFound = true);
        ptrdiff_t find(const string_view str, size_t sanitySize = ~0, bool throwIfNotFound = true);
        void unread(const Buffer *buff, size_t len);

      protected:
        explicit MutatingFilterStream(Stream::ptr parent, bool owns = true) : FilterStream(parent, owns) {}
      };
    }  // namespace streams
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_STREAMS_FILTER_HH_
