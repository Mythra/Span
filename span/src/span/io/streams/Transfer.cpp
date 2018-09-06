#include "span/io/streams/Transfer.hh"

#include <vector>

#include "span/Common.hh"
#include "span/exceptions/Assert.hh"
#include "span/fibers/Fiber.hh"
#include "span/io/streams/Buffer.hh"
#include "span/io/streams/Null.hh"
#include "span/io/streams/Stream.hh"
#include "span/Parallel.hh"

#include "glog/logging.h"

namespace span {
  namespace io {
    namespace streams {
      // TODO(kfc): Make this configurable?
      static size_t g_chunkSize = 65536;

      static void readOne(Stream *src, Buffer *buff, size_t len, size_t *result) {
        *result = src->read(buff, len);
        DLOG(INFO) << "read " << *result << " bytes from " << src;
      }

      static void writeOne(Stream *dst, Buffer *buff) {
        size_t result;
        while (buff->readAvailable() > 0) {
          result = dst->write(buff, buff->readAvailable());
          DLOG(INFO) << "wrote " << result << " bytes to " << dst;
          buff->consume(result);
        }
      }

      uint64 transferStream(Stream *src, Stream *dst, uint64 toTransfer, ExactLength exactLength) {
        DLOG(INFO) << "transferring " << toTransfer << " bytes from " << src << " to " << dst;
        SPAN_ASSERT(src->supportsRead());
        SPAN_ASSERT(dst->supportsWrite());

        Buffer buff, buff_two;
        Buffer *readBuffer, *writeBuffer;
        size_t chunkSize = g_chunkSize;
        size_t todo;
        size_t readResult;
        uint64 totalRead = 0;

        if (toTransfer == 0) {
          return 0;
        }
        if (exactLength == INFER) {
          exactLength = (toTransfer == ~0ull ? UNTILEOF : EXACT);
        }
        SPAN_ASSERT(exactLength == EXACT || exactLength == UNTILEOF);

        readBuffer = &buff;
        todo = chunkSize;

        if (toTransfer - totalRead < static_cast<uint64>(todo)) {
          todo = static_cast<size_t>(toTransfer - totalRead);
        }
        readOne(src, readBuffer, todo, &readResult);
        totalRead += readResult;

        if (readResult == 0 && exactLength == EXACT) {
          throw std::runtime_error("Unexepected Eof Exception");
        }
        if (readResult == 0) {
          return totalRead;
        }

        // Optimize transfer to NullStream
        if (dst == NullStream::get_ptr().get()) {
          while (true) {
            readBuffer->clear();
            todo = chunkSize;

            if (toTransfer - totalRead < static_cast<uint64>(todo)) {
              todo = static_cast<size_t>(toTransfer - totalRead);
            }
            if (todo == 0) {
              return totalRead;
            }

            readOne(src, readBuffer, todo, &readResult);
            totalRead += readResult;
            if (readResult == 0 && exactLength == EXACT) {
              throw std::runtime_error("Unexpected EoF Exception");
            }
            if (readResult == 0) {
              return totalRead;
            }
          }
        }

        std::vector<std::function<void()>> dgs;
        std::vector<span::fibers::Fiber::ptr> fibers;

        dgs.resize(2);
        fibers.resize(2);

        fibers.at(0).reset(new span::fibers::Fiber(NULL));
        fibers.at(1).reset(new span::fibers::Fiber(NULL));
        dgs[0] = std::bind(&readOne, src, readBuffer, todo, &readResult);
        dgs[1] = std::bind(&writeOne, dst, std::ref(writeBuffer));

        while (totalRead < toTransfer) {
          writeBuffer = readBuffer;
          if (readBuffer == &buff) {
            readBuffer = &buff_two;
          } else {
            readBuffer = &buff;
          }
          todo = chunkSize;
          if (toTransfer - totalRead < static_cast<uint64>(todo)) {
            todo = static_cast<size_t>(toTransfer - totalRead);
          }
          span::parallel_do(&dgs, &fibers);
          totalRead += readResult;
          if (readResult == 0 && exactLength == EXACT && totalRead < toTransfer) {
            LOG(ERROR) << "only read " << totalRead << "/" << toTransfer << " from " << src;
          }
          if (readResult == 0) {
            return totalRead;
          }
        }

        writeBuffer = readBuffer;
        writeOne(dst, writeBuffer);
        DLOG(INFO) << "transferred " << totalRead << "/" << toTransfer << " from " << src << " to " << dst;
        return totalRead;
      }
    }  // namespace streams
  }  // namespace io
}  // namespace span
