#include "span/io/streams/Filter.hh"

#if __has_include(<string_view>)
#include <string_view>
using std::string_view;
#else
#include <experimental/string_view>
using std::experimental::string_view;
#endif

#include "span/Common.hh"
#include "span/exceptions/Assert.hh"

namespace span {
  namespace io {
    namespace streams {
      int64 MutatingFilterStream::seek(int64 offset, Anchor anchor) {
        SPAN_NOT_REACHED("MutatingFilterStream::seek");
      }

      int64 MutatingFilterStream::size() {
        SPAN_NOT_REACHED("MutatingFilterStream::size");
      }

      void MutatingFilterStream::truncate(int64 size) {
        SPAN_NOT_REACHED("MutatingFilterStream::truncate");
      }

      ptrdiff_t MutatingFilterStream::find(char delim, size_t sanitySize, bool throwIfNotFound) {
        SPAN_NOT_REACHED("MutatingFilterStream::find");
      }
      ptrdiff_t MutatingFilterStream::find(const string_view str, size_t sanitySize, bool throwIfNotFound) {
        SPAN_NOT_REACHED("MutatingFilterStream::find");
      }

      void MutatingFilterStream::unread(const Buffer *buff, size_t len) {
        SPAN_NOT_REACHED("MutatingFilterStream::unread");
      }
    }  // namespace streams
  }  // namespace io
}  // namespace span
