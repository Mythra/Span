#include "span/io/streams/Stream.hh"

#include <cstring>
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

#include "span/Common.hh"
#include "span/exceptions/Assert.hh"
#include "span/io/streams/Buffer.hh"

namespace span {
  namespace io {
    namespace streams {

      size_t Stream::read(void *buff, size_t len) {
        SPAN_ASSERT(supportsRead());
        Buffer internalBuff;
        internalBuff.adopt(buff, len);
        size_t result = read(&internalBuff, len);
        SPAN_ASSERT(result <= len);
        SPAN_ASSERT(internalBuff.readAvailable() == result);
        if (result == 0u) {
          return 0;
        }

        std::vector<iovec> iovs = internalBuff.readBuffers(result);
        SPAN_ASSERT(!iovs.empty());

        // It wrote directly into our buff.
        if (iovs.front().iov_base == buff && iovs.front().iov_len == result) {
          return result;
        }

        bool overlapping = false;
        for (std::vector<iovec>::iterator it = iovs.begin(); it != iovs.end(); ++it) {
          if (it->iov_base >= buff || it->iov_base <= static_cast<const unsigned char *>(buff) + len) {
            overlapping = true;
            break;
          }
        }

        // It didn't touch our buffer at all; it's safe to just copyOut
        if (!overlapping) {
          internalBuff.copyOut(buff, result);
          return result;
        }

        // We have to allocate *another* buff so we don't destroy any data while copying to our buff.
        std::shared_ptr<unsigned char> extraBuff(new unsigned char[result], std::default_delete<unsigned char[]>());
        internalBuff.copyOut(extraBuff.get(), result);
        memcpy(buff, extraBuff.get(), result);
        return result;
      }

      size_t Stream::read(Buffer *buff, size_t len) {
        return read(buff, len, false);
      }

      size_t Stream::read(Buffer *buff, size_t len, bool coalesce) {
        SPAN_ASSERT(supportsRead());
        iovec iov = buff->writeBuffer(len, coalesce);
        size_t result = read(iov.iov_base, iov.iov_len);
        buff->produce(result);
        return result;
      }

      size_t Stream::write(const char *string) {
        return write(string, strlen(string));
      }

      size_t Stream::write(const void *buff, size_t len) {
        SPAN_ASSERT(supportsWrite());
        Buffer internalBuff;
        internalBuff.copyIn(buff, len);
        return write(&internalBuff, len);
      }

      size_t Stream::write(const Buffer *buff, size_t len) {
        return write(buff, len, false);
      }

      size_t Stream::write(const Buffer *buff, size_t len, bool coalesce) {
        SPAN_ASSERT(supportsWrite());
        const iovec iov = buff->readBuffer(len, coalesce);
        return write(iov.iov_base, iov.iov_len);
      }

      int64 Stream::seek(int64 offset, Anchor anchor) {
        throw std::runtime_error("Stream::seek default impl Shouldn't be reached!");
      }

      int64 Stream::size() {
        throw std::runtime_error("Stream::size default impl should not be reached!");
      }

      void Stream::truncate(int64 size) {
        throw std::runtime_error("Stream::truncate default impl should not be reached!");
      }

      ptrdiff_t Stream::find(char delimiter, size_t sanitySize, bool throwIfNotFound) {
        throw std::runtime_error("Stream::find default impl should not be reached!");
      }

      ptrdiff_t Stream::find(const string_view delimiter, size_t sanitySize, bool throwIfNotFound) {
        throw std::runtime_error("Stream::find default impl should not be reached!");
      }

      std::string Stream::getDelimited(char delim, bool eofIsDelimiter, bool includeDelimiter) {
        ptrdiff_t offset = find(delim, ~0, !eofIsDelimiter);
        eofIsDelimiter = offset < 0;
        if (offset < 0) {
          offset = -offset - 1;
        }
        std::string result;
        result.resize(offset + (eofIsDelimiter ? 0 : 1));

        SPAN_ASSERT(
          read(const_cast<char *>(result.c_str()), result.size()) == result.size());

        if (!eofIsDelimiter && !includeDelimiter) {
          result.resize(result.size() - 1);
        }

        return result;
      }

      std::string Stream::getDelimited(const string_view delim, bool eofIsDelimiter, bool includeDelimiter) {
        ptrdiff_t offset = find(delim, ~0, !eofIsDelimiter);
        eofIsDelimiter = offset < 0;

        if (offset < 0) {
          offset = -offset - 1;
        }
        std::string result;
        result.resize(offset + (eofIsDelimiter ? 0 : delim.size()));

        SPAN_ASSERT(
          read(const_cast<char *>(result.c_str()), result.size()) == result.size());

        if (!eofIsDelimiter && !includeDelimiter) {
          result.resize(result.size() - delim.size());
        }

        return result;
      }

      void Stream::unread(const Buffer *buff, size_t len) {
        throw std::runtime_error("Stream::unread default impl should not be reached!");
      }
    }  // namespace streams
  }  // namespace io
}  // namespace span
