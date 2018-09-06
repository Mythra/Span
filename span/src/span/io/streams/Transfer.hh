#ifndef SPAN_SRC_SPAN_IO_STREAMS_TRANSFER_HH_
#define SPAN_SRC_SPAN_IO_STREAMS_TRANSFER_HH_

#include "span/Common.hh"
#include "span/io/streams/Stream.hh"

namespace span {
  namespace io {
    namespace streams {
      enum ExactLength {
        // toTransfer == ~0ull, use EOF, otherwise EXACT
        INFER,
        // If toTransfer bytes can't be read, throw UnexpectedEofException
        EXACT,
        // Trnasfer as many bytes as possible until EOF is hit.
        UNTILEOF
      };

      uint64 transferStream(Stream *src, Stream *ds, uint64 toTransfer = ~0ull, ExactLength exactLength = INFER);

      inline uint64 transferStream(Stream::ptr src, Stream *dst, uint64 toTransfer = ~0ull,
        ExactLength exactLength = INFER) {
        return transferStream(src.get(), dst, toTransfer, exactLength);
      }

      inline uint64 transferStream(Stream *src, Stream::ptr dst, uint64 toTransfer = ~0ull,
        ExactLength exactLength = INFER) {
        return transferStream(src, dst.get(), toTransfer, exactLength);
      }

      inline uint64 transferStream(Stream::ptr src, Stream::ptr dst, uint64 toTransfer = ~0ull,
        ExactLength exactLength = INFER) {
        return transferStream(src.get(), dst.get(), toTransfer, exactLength);
      }
    }  // namespace streams
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_STREAMS_TRANSFER_HH_
