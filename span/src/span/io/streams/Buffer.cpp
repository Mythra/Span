#include "span/io/streams/Buffer.hh"

#include <algorithm>
#include <list>
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

namespace span {
  namespace io {
    namespace streams {
      Buffer::SegmentData::SegmentData() {
        start(NULL);
        len(0);
      }

      Buffer::SegmentData::SegmentData(size_t len) {
        array_.reset(new unsigned char[len], std::default_delete<unsigned char[]>());
        start(array_.get());
        this->len(len);
      }

      Buffer::SegmentData::SegmentData(void *buff, size_t len) {
        array_.reset(static_cast<unsigned char *>(buff), &nop<unsigned char *>);
        start(array_.get());
        this->len(len);
      }

      Buffer::SegmentData Buffer::SegmentData::slice(size_t start, size_t len) {
        if (len == static_cast<size_t>(~0)) {
          len = this->len() - start;
        }
        SPAN_ASSERT(start <= this->len());
        SPAN_ASSERT(len + start <= this->len());

        SegmentData result;
        result.array_ = array_;
        result.start(static_cast<unsigned char*>(this->start()) + start);
        result.len(len);

        return result;
      }

      const Buffer::SegmentData Buffer::SegmentData::slice(size_t start, size_t len) const {
        if (len == static_cast<size_t>(~0)) {
          len = this->len() - start;
        }
        SPAN_ASSERT(start <= this->len());
        SPAN_ASSERT(len + start <= this->len());
        SegmentData result;
        result.array_ = array_;
        result.start(const_cast<unsigned char*>(static_cast<const unsigned char*>(this->start())) + start);
        result.len(len);
        return result;
      }

      void Buffer::SegmentData::extend(size_t len) {
        // What's the chance someone could ever overrun their buffer :shiftyeyes:
        len_ += len;
      }

      Buffer::Segment::Segment(size_t len) : writeIndex_(0), data_(len) {
        invariant();
      }

      Buffer::Segment::Segment(Buffer::SegmentData data) : writeIndex_(data.len()), data_(data) {
        invariant();
      }

      Buffer::Segment::Segment(void *buff, size_t len) : writeIndex_(0), data_(buff, len) {
        invariant();
      }

      size_t Buffer::Segment::readAvailable() const {
        invariant();
        return writeIndex_;
      }

      size_t Buffer::Segment::writeAvailable() const {
        invariant();
        return data_.len() - writeIndex_;
      }

      size_t Buffer::Segment::len() const {
        invariant();
        return data_.len();
      }

      void Buffer::Segment::produce(size_t len) {
        SPAN_ASSERT(len <= writeAvailable());
        writeIndex_ += len;
        invariant();
      }

      void Buffer::Segment::consume(size_t len) {
        SPAN_ASSERT(len <= readAvailable());

        writeIndex_ -= len;
        data_ = data_.slice(len);

        invariant();
      }

      void Buffer::Segment::truncate(size_t len) {
        SPAN_ASSERT(len <= readAvailable());
        SPAN_ASSERT(writeIndex_ = readAvailable());

        writeIndex_ = len;
        data_ = data_.slice(0, len);
        invariant();
      }

      void Buffer::Segment::extend(size_t len) {
        data_.extend(len);
        writeIndex_ += len;
      }

      const Buffer::SegmentData Buffer::Segment::readBuffer() const {
        invariant();
        return data_.slice(0, writeIndex_);
      }

      Buffer::SegmentData Buffer::Segment::writeBuffer() {
        invariant();
        return data_.slice(writeIndex_);
      }

      const Buffer::SegmentData Buffer::Segment::writeBuffer() const {
        invariant();
        return data_.slice(writeIndex_);
      }

      void Buffer::Segment::invariant() const {
        SPAN_ASSERT(writeIndex_ <= data_.len());
      }

      Buffer::Buffer() {
        readAvailable_ = writeAvailable_ = 0;
        writeIt_ = segments_.end();
        invariant();
      }

      Buffer::Buffer(const Buffer *copy) {
          readAvailable_ = writeAvailable_ = 0;
          writeIt_ = segments_.end();
          copyIn(copy);
      }

      Buffer::Buffer(const string_view view) {
        readAvailable_ = writeAvailable_ = 0;
        writeIt_ = segments_.end();
        copyIn(view);
      }

      Buffer::Buffer(const void *data, size_t len) {
        readAvailable_ = writeAvailable_ = 0;
        writeIt_ = segments_.end();
        copyIn(data, len);
      }

      Buffer &Buffer::operator= (const Buffer *copy) {
        clear();
        copyIn(copy);
        return *this;
      }

      size_t Buffer::readAvailable() const {
        return readAvailable_;
      }

      size_t Buffer::writeAvailable() const {
        return writeAvailable_;
      }

      size_t Buffer::segments() const {
        return segments_.size();
      }

      void Buffer::adopt(void *buff, size_t len) {
        invariant();
        Segment newSegment(buff, len);
        if (readAvailable() == 0) {
          // put the new buffer at the front if possible to avoid
          // fragementation
          segments_.push_front(newSegment);
          writeIt_ = segments_.begin();
        } else {
          segments_.push_back(newSegment);
          if (writeAvailable_ == 0) {
            writeIt_ = segments_.end();
            --writeIt_;
          }
        }

        writeAvailable_ += len;
        invariant();
      }

      void Buffer::reserve(size_t len) {
        if (writeAvailable() < len) {
          // over-reserve to avoid fragmentation.
          Segment newSegment(len * 2 - writeAvailable());
          if (readAvailable() == 0) {
            // don't fragment
            segments_.push_front(newSegment);
            writeIt_ = segments_.begin();
          } else {
            segments_.push_back(newSegment);
            if (writeAvailable_ == 0) {
              writeIt_ = segments_.end();
              --writeIt_;
            }
          }

          writeAvailable_ += newSegment.len();
          invariant();
        }
      }

      void Buffer::compact() {
        invariant();
        if (writeIt_ != segments_.end()) {
          if (writeIt_->readAvailable() > 0) {
            Segment newSegment = Segment(writeIt_->readBuffer());
            segments_.insert(writeIt_, newSegment);
          }
          writeIt_ = segments_.erase(writeIt_, segments_.end());
          writeAvailable_ = 0;
        }
        SPAN_ASSERT(writeAvailable() == 0);
      }

      void Buffer::clear(bool clearWriteAvailableAsWell) {
        invariant();
        if (clearWriteAvailableAsWell) {
          readAvailable_ = writeAvailable_ = 0;
          segments_.clear();
          writeIt_ = segments_.end();
        } else {
          readAvailable_ = 0;
          if (writeIt_ != segments_.end() && writeIt_->readAvailable()) {
            writeIt_->consume(writeIt_->readAvailable());
          }
          segments_.erase(segments_.begin(), writeIt_);
        }
        invariant();
        SPAN_ASSERT(readAvailable_ == 0);
      }

      void Buffer::produce(size_t len) {
        SPAN_ASSERT(len <= writeAvailable());
        readAvailable_ += len;
        writeAvailable_ -= len;
        while (len > 0) {
          Segment &segment = *writeIt_;
          size_t toProduce = std::min(segment.writeAvailable(), len);
          segment.produce(toProduce);
          len -= toProduce;
          if (segment.writeAvailable() == 0) {
            writeIt_++;
          }
        }
        SPAN_ASSERT(len == 0);
        invariant();
      }

      void Buffer::consume(size_t len) {
        SPAN_ASSERT(len <= readAvailable());
        readAvailable_ -= len;
        while (len > 0) {
          Segment *segment = &(*segments_.begin());
          size_t toConsume = std::min(segment->readAvailable(), len);
          segment->consume(toConsume);
          len -= toConsume;
          if (segment->len() == 0) {
            segments_.pop_front();
          }
        }
        SPAN_ASSERT(len == 0);
        invariant();
      }

      void Buffer::truncate(size_t len) {
        SPAN_ASSERT(len <= readAvailable());
        if (len == readAvailable_) {
          return;
        }
        // Split any mixed read/write buffs.
        if (writeIt_ != segments_.end() && writeIt_->readAvailable() != 0) {
          segments_.insert(writeIt_, Segment(writeIt_->readBuffer()));
          writeIt_->consume(writeIt_->readAvailable());
        }
        readAvailable_ = len;
        std::list<Segment>::iterator it;
        for (it = segments_.begin(); it != segments_.end() && len > 0; ++it) {
          Segment segment = *it;
          if (len <= segment.readAvailable()) {
            segment.truncate(len);
            len = 0;
            ++it;
            break;
          } else {
            len -= segment.readAvailable();
          }
        }

        SPAN_ASSERT(len == 0);
        while (it != segments_.end() && it->readAvailable() > 0) {
          SPAN_ASSERT(it->writeAvailable() == 0);
          it = segments_.erase(it);
        }

        invariant();
      }

      const std::vector<iovec> Buffer::readBuffers(size_t len) const {
        if (len == static_cast<size_t>(~0)) {
          len = readAvailable();
        }
        SPAN_ASSERT(len <= readAvailable());
        std::vector<iovec> result;

        result.reserve(segments_.size());
        size_t remaining  = len;
        std::list<Segment>::const_iterator it;
        for (it = segments_.begin(); it != segments_.end(); ++it) {
          size_t toConsume = std::min(it->readAvailable(), remaining);
          SegmentData data = it->readBuffer().slice(0, toConsume);

#if PLATFORM != PLATFORM_WIN32
          iovec iov;
          iov.iov_base = static_cast<void *>(data.start());
          iov.iov_len = data.len();
          result.push_back(iov);
#endif

          remaining -= toConsume;
          if (remaining == 0) {
            break;
          }
        }

        SPAN_ASSERT(remaining == 0);
        invariant();
        return result;
      }

      const iovec Buffer::readBuffer(size_t len, bool coalesce) const {
        iovec result;
        result.iov_base = NULL;
        result.iov_len = 0;
        if (len == static_cast<size_t>(~0)) {
          len = readAvailable();
        }
        SPAN_ASSERT(len <= readAvailable());
        if (readAvailable() == 0) {
          return result;
        }

        // Optimize case where all that is requested is contained in the first buffer.
        if (segments_.front().readAvailable() >= len) {
          SegmentData data = segments_.front().readBuffer().slice(0, len);
          result.iov_base = data.start();
          result.iov_len = data.len();
          return result;
        }

        // If they don't want us to coalesce just return as much as we can from the first segment.
        if (!coalesce) {
          SegmentData data = segments_.front().readBuffer();
          result.iov_base = data.start();
          result.iov_len = data.len();
          return result;
        }

        // Breaking constness!
        Buffer* _this = const_cast<Buffer*>(this);

        // try to avoid allocation
        if (writeIt_ != segments_.end() && writeIt_->writeAvailable() >= readAvailable()) {
          copyOut(writeIt_->writeBuffer().start(), readAvailable());
          Segment newSegment = Segment(writeIt_->writeBuffer().slice(0, readAvailable()));
          _this->segments_.clear();
          _this->segments_.push_back(newSegment);
          _this->writeAvailable_ = 0;
          _this->writeIt_ = _this->segments_.end();
          invariant();
          SegmentData data = newSegment.readBuffer().slice(0, len);
          result.iov_base = data.start();
          result.iov_len = data.len();
          return result;
        }

        Segment newSegment = Segment(readAvailable());
        copyOut(newSegment.writeBuffer().start(), readAvailable());
        newSegment.produce(readAvailable());
        _this->segments_.clear();
        _this->segments_.push_back(newSegment);
        _this->writeAvailable_ = 0;
        _this->writeIt_ = _this->segments_.end();
        invariant();

        SegmentData data = newSegment.readBuffer().slice(0, len);
        result.iov_base = data.start();
        result.iov_len = data.len();

        return result;
      }

      std::vector<iovec> Buffer::writeBuffers(size_t len) {
        if (len == static_cast<size_t>(~0)) {
          len = writeAvailable();
        }
        reserve(len);

        std::vector<iovec> result;
        result.reserve(segments_.size());
        size_t remaining = len;
        std::list<Segment>::iterator it = writeIt_;
        while (remaining > 0) {
          Segment segment = *it;
          size_t toProduce = std::min(segment.writeAvailable(), remaining);
          SegmentData data = segment.writeBuffer().slice(0, toProduce);
#if PLATFORM != PLATFORM_WIN32
          iovec iov;
          iov.iov_base = static_cast<void *>(data.start());
          iov.iov_len = data.len();
          result.push_back(iov);
#endif
          remaining -= toProduce;
          ++it;
        }
        SPAN_ASSERT(remaining == 0);
        invariant();
        return result;
      }

      iovec Buffer::writeBuffer(size_t len, bool coalesce) {
        iovec result;
        result.iov_base = NULL;
        result.iov_len = 0;
        if (len == 0u) {
          return result;
        }

        // Must allocate just the write segment.
        if (writeAvailable() == 0) {
          reserve(len);
          SPAN_ASSERT(writeIt_ != segments_.end());
          SPAN_ASSERT(writeIt_->writeAvailable() >= len);
          SegmentData data = writeIt_->writeBuffer().slice(0, len);
          result.iov_base = data.start();
          result.iov_len = data.len();
          return result;
        }

        // Can we use an existing write segment
        if (writeAvailable() > 0 && writeIt_->writeAvailable() >= len) {
          SegmentData data = writeIt_->writeBuffer().slice(0, len);
          result.iov_base = data.start();
          result.iov_len = data.len();
          return result;
        }

        // If they don't want us to coalesce, just return as much as we can from first segment.
        if (!coalesce) {
          SegmentData data = writeIt_->writeBuffer();
          result.iov_base = data.start();
          result.iov_len = data.len();
          return result;
        }

        // Existing buffs are insufficient.... remove them and reserve anew.
        compact();
        reserve(len);

        SPAN_ASSERT(writeIt_ != segments_.end());
        SPAN_ASSERT(writeIt_->writeAvailable() >= len);

        SegmentData data = writeIt_->writeBuffer().slice(0, len);
        result.iov_base = data.start();
        result.iov_len = data.len();
        return result;
      }

      void Buffer::copyIn(const Buffer *buffer, size_t len, size_t pos) {
        if (pos > buffer->readAvailable()) {
          throw std::out_of_range("position out of range!");
        }
        if (len == static_cast<size_t>(~0)) {
          len = buffer->readAvailable() - pos;
        }
        SPAN_ASSERT(buffer->readAvailable() >= len + pos);
        invariant();
        if (len == 0) {
          return;
        }

        // Split any mixed read/write buffs.
        if (writeIt_ != segments_.end() && writeIt_->readAvailable() != 0) {
          segments_.insert(writeIt_, Segment(writeIt_->readBuffer()));
          writeIt_->consume(writeIt_->readAvailable());
          invariant();
        }

        std::list<Segment>::const_iterator it = buffer->segments_.begin();
        while (pos != 0 && it != buffer->segments_.end()) {
          if (pos < it->readAvailable()) {
            break;
          }
          pos -= it->readAvailable();
          ++it;
        }
        SPAN_ASSERT(it != buffer->segments_.end());

        for (; it != buffer->segments_.end(); ++it) {
          size_t toConsume = std::min(it->readAvailable() - pos, len);
          if (readAvailable_ != 0 && it == buffer->segments_.begin()) {
            std::list<Segment>::iterator previousIt = writeIt_;
            --previousIt;
            if (const_cast<char *>(static_cast<const char *>(previousIt->readBuffer().start()))
              + previousIt->readBuffer().len() ==
              const_cast<char *>(static_cast<const char *>(it->readBuffer().start()))
              + pos && previousIt->data_.array_.get() == it->data_.array_.get()) {
              SPAN_ASSERT(previousIt->writeAvailable() == 0);
              previousIt->extend(toConsume);
              readAvailable_ += toConsume;
              len -= toConsume;
              pos = 0;
              if (len == 0) {
                break;
              }
              continue;
            }
          }

          Segment newSegment = Segment(it->readBuffer().slice(pos, toConsume));
          segments_.insert(writeIt_, newSegment);
          readAvailable_ += toConsume;
          len -= toConsume;
          pos = 0;
          if (len == 0) {
            break;
          }
        }

        SPAN_ASSERT(len == 0);
        SPAN_ASSERT(readAvailable() >= len);
      }

      void Buffer::copyIn(const void *data, size_t len) {
        invariant();

        while (writeIt_ != segments_.end() && len > 0) {
          size_t todo = std::min(len, writeIt_->writeAvailable());
          memcpy(writeIt_->writeBuffer().start(), data, todo);
          writeIt_->produce(todo);
          writeAvailable_ -= todo;
          readAvailable_ += todo;
          data = static_cast<const unsigned char*>(data) + todo;
          len -= todo;
          if (writeIt_->writeAvailable() == 0) {
            ++writeIt_;
          }
          invariant();
        }

        if (len > 0) {
          Segment newSegment(len);
          memcpy(newSegment.writeBuffer().start(), data, len);
          newSegment.produce(len);
          segments_.push_back(newSegment);
          readAvailable_ += len;
        }

        SPAN_ASSERT(readAvailable() >= len);
      }

      void Buffer::copyIn(const string_view view) {
        return copyIn(view.data(), view.size());
      }

      void Buffer::copyOut(void *buffer, size_t len, size_t pos) const {
        if (len == 0) {
          return;
        }

        SPAN_ASSERT(len + pos <= readAvailable());
        unsigned char *next = static_cast<unsigned char *>(buffer);
        std::list<Segment>::const_iterator it = segments_.begin();
        while (pos != 0 && it != segments_.end()) {
          if (pos < it->readAvailable()) {
            break;
          }
          pos -= it->readAvailable();
          ++it;
        }
        SPAN_ASSERT(it != segments_.end());

        for (; it != segments_.end(); ++it) {
          size_t todo = std::min(len, it->readAvailable() - pos);
          memcpy(next, const_cast<char *>(static_cast<const char*>(it->readBuffer().start())) + pos, todo);
          next += todo;
          len -= todo;
          pos = 0;
          if (len == 0) {
            break;
          }
        }

        SPAN_ASSERT(len == 0);
      }

      ptrdiff_t Buffer::find(char delimiter, size_t len) const {
        if (len == static_cast<size_t>(~0)) {
          len = readAvailable();
        }
        SPAN_ASSERT(len <= readAvailable());

        size_t totalLen = 0;
        bool success = false;

        std::list<Segment>::const_iterator it;
        for (it = segments_.begin(); it != segments_.end(); ++it) {
          const void *start = it->readBuffer().start();
          size_t toScan = std::min(len, it->readAvailable());
          const void *point = memchr(start, delimiter, toScan);
          if (point != NULL) {
            success = true;
            totalLen += static_cast<const unsigned char*>(point) - static_cast<const unsigned char*>(start);
            break;
          }
          totalLen += toScan;
          len -= toScan;
          if (len == 0) {
            break;
          }
        }

        if (success) {
          return totalLen;
        }

        return -1;
      }

      ptrdiff_t Buffer::find(const string_view view, size_t len) const {
        if (len == static_cast<size_t>(~0)) {
          len = readAvailable();
        }
        SPAN_ASSERT(len <= readAvailable());
        SPAN_ASSERT(!view.empty());

        size_t totalLen = 0;
        size_t foundSoFar = 0;

        std::list<Segment>::const_iterator it;
        for (it = segments_.begin(); it != segments_.end(); ++it) {
          const void *start = it->readBuffer().start();
          size_t toScan = std::min(len, it->readAvailable());
          while (toScan > 0) {
            if (foundSoFar == 0) {
              const void *point = memchr(start, view.at(0), toScan);
              if (point != NULL) {
                foundSoFar = 1;
                size_t found = static_cast<const unsigned char*>(point) - static_cast<const unsigned char*>(start);
                toScan -= found + 1;
                len -= found + 1;
                totalLen += found;
                start = static_cast<const unsigned char *>(point) + 1;
              } else {
                totalLen += toScan;
                len -= toScan;
                toScan = 0;
                continue;
              }
            }

            SPAN_ASSERT(foundSoFar != 0);
            size_t toCompare = std::min(toScan, view.size() - foundSoFar);
            if (memcmp(start, view.data() + foundSoFar, toCompare) == 0) {
              foundSoFar += toCompare;
              toScan -= toCompare;
              len -= toCompare;
              if (foundSoFar == view.size()) {
                break;
              }
            } else {
              totalLen += foundSoFar;
              foundSoFar = 0;
            }
          }

          if (foundSoFar == view.size()) {
            break;
          }
          if (len == 0) {
            break;
          }
        }

        if (foundSoFar == view.size()) {
          return totalLen;
        }
        return -1;
      }

      std::string Buffer::to_string() const {
        if (readAvailable_ == 0) {
          return std::string();
        }

        std::string result;
        result.resize(readAvailable_);
        copyOut(&result[0], result.size());
        return result;
      }

      std::string Buffer::getDelimited(char delimiter, bool eofIsDelimiter, bool includeDelimiter) {
        ptrdiff_t offset = find(delimiter, ~0);
        SPAN_ASSERT(offset >= -1);
        if (offset == -1 && !eofIsDelimiter) {
          throw std::runtime_error("Unexcpected EOF!");
        }
        eofIsDelimiter = offset == -1;

        if (offset == -1) {
          offset = readAvailable();
        }

        std::string result;
        result.resize(offset + (eofIsDelimiter ? 0 : (includeDelimiter ? 1 : 0)));
        copyOut(&result[0], result.size());
        consume(result.size());
        if (!eofIsDelimiter && !includeDelimiter) {
          consume(1u);
        }

        return result;
      }

      std::string Buffer::getDelimited(const string_view delimiter, bool eofIsDelimiter, bool includeDelimiter) {
        ptrdiff_t offset = find(delimiter, ~0);
        SPAN_ASSERT(offset >= -1);
        if (offset == -1 && !eofIsDelimiter) {
          throw std::runtime_error("Unexpected EOF!");
        }
        eofIsDelimiter = offset == -1;

        if (offset == -1) {
          offset = readAvailable();
        }

        std::string result;
        result.resize(offset + (eofIsDelimiter ? 0 : (includeDelimiter ? delimiter.size() : 0)));
        copyOut(&result[0], result.size());
        consume(result.size());

        if (!eofIsDelimiter && !includeDelimiter) {
          consume(delimiter.size());
        }

        return result;
      }

      void Buffer::visit(std::function<void(const void *, size_t)> dg, size_t len) const {
        if (len == static_cast<size_t>(~0)) {
          len = readAvailable();
        }
        SPAN_ASSERT(len <= readAvailable());

        std::list<Segment>::const_iterator it;
        for (it = segments_.begin(); it != segments_.end() && len > 0; ++it) {
          size_t todo = std::min(len, it->readAvailable());
          SPAN_ASSERT(todo != 0);
          dg(it->readBuffer().start(), todo);
          len -= todo;
        }
        SPAN_ASSERT(len == 0);
      }

      bool Buffer::operator== (const Buffer &rhs) const {
        if (rhs.readAvailable() != readAvailable()) {
          return false;
        }
        return opCmp(&rhs) == 0;
      }

      bool Buffer::operator!= (const Buffer &rhs) const {
        if (rhs.readAvailable() != readAvailable()) {
          return true;
        }
        return opCmp(&rhs) != 0;
      }

      bool Buffer::operator== (const string_view view) const {
        if (view.size() != readAvailable()) {
          return false;
        }
        return opCmp(view.data(), view.size()) == 0;
      }

      bool Buffer::operator!= (const string_view view) const {
        if (view.size() != readAvailable()) {
          return true;
        }
        return opCmp(view.data(), view.size()) != 0;
      }

      int Buffer::opCmp(const Buffer *rhs) const {
        std::list<Segment>::const_iterator leftIt, rightIt;
        int lenResult = static_cast<int>(static_cast<ptrdiff_t>(readAvailable()) -
          static_cast<ptrdiff_t>(rhs->readAvailable()));
        leftIt = segments_.begin(); rightIt = rhs->segments_.begin();
        size_t leftOffset = 0, rightOffset = 0;

        while (leftIt != segments_.end() && rightIt != rhs->segments_.end()) {
          SPAN_ASSERT(leftOffset <= leftIt->readAvailable());
          SPAN_ASSERT(rightOffset <= rightIt->readAvailable());

          size_t toCompare = std::min(leftIt->readAvailable() - leftOffset, rightIt->readAvailable() - rightOffset);
          if (toCompare == 0) {
            break;
          }

          int result = memcmp(static_cast<const unsigned char *>(leftIt->readBuffer().start()) + leftOffset,
            static_cast<const unsigned char *>(rightIt->readBuffer().start()) + rightOffset,
            toCompare);
          if (result != 0) {
            return result;
          }

          leftOffset += toCompare;
          rightOffset += toCompare;

          if (leftOffset == leftIt->readAvailable()) {
            leftOffset = 0;
            ++leftIt;
          }
          if (rightOffset == rightIt->readAvailable()) {
            rightOffset = 0;
            ++rightIt;
          }
        }

        return lenResult;
      }

      int Buffer::opCmp(string_view view, size_t len) const {
        size_t offset = 0;
        std::list<Segment>::const_iterator it;
        int lenResult = static_cast<int>(static_cast<ptrdiff_t>(readAvailable()) - static_cast<ptrdiff_t>(len));
        if (lenResult > 0) {
          len = readAvailable();
        }

        for (it = segments_.begin(); it != segments_.end(); ++it) {
          size_t toCompare = std::min(it->readAvailable(), len);
          int result = memcmp(it->readBuffer().start(), view.data() + offset, toCompare);
          if (result != 0) {
            return result;
          }
          len -= toCompare;
          offset += toCompare;

          if (len == 0) {
            return lenResult;
          }
        }

        return lenResult;
      }

      // TODO(kfc): Port over with flag about including debug info even though perf.
      void Buffer::invariant() const { }
    }  // namespace streams
  }  // namespace io
}  // namespace span
