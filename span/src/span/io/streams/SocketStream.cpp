#include "span/io/streams/SocketStream.hh"

#include <vector>

#include "span/exceptions/Assert.hh"
#include "span/io/streams/Buffer.hh"
#include "span/io/Socket.hh"

namespace span {
  namespace io {
    namespace streams {
      SocketStream::SocketStream(span::io::Socket::ptr socket, bool own) : socket_(socket), own_(own) {
        SPAN_ASSERT(socket);
      }

      void SocketStream::close(CloseType type) {
        if (socket_ && own_) {
          int how;
          switch (type) {
            case READ:
              how = SHUT_RD;
              break;
            case WRITE:
              how = SHUT_WR;
              break;
            default:
              how = SHUT_RDWR;
              break;
          }
          socket_->shutdown(how);
        }
      }

      size_t SocketStream::read(Buffer *buff, size_t len) {
        std::vector<iovec> iovs = buff->writeBuffers(len);
        size_t result = socket_->receive(&iovs[0], iovs.size());
        buff->produce(result);
        return result;
      }

      size_t SocketStream::read(void *buff, size_t len) {
        return socket_->receive(buff, len);
      }

      void SocketStream::cancelRead() {
        socket_->cancelReceive();
      }

      size_t SocketStream::write(const Buffer *buff, size_t len) {
        const std::vector<iovec> iovs = buff->readBuffers(len);
        size_t result = socket_->send(&iovs[0], iovs.size());
        SPAN_ASSERT(result > 0);
        return result;
      }

      size_t SocketStream::write(const void *buff, size_t len) {
        return socket_->send(buff, len);
      }

      void SocketStream::cancelWrite() {
        socket_->cancelSend();
      }

      slimsig::signal_t<void()>::connection SocketStream::onRemoteClose(const std::function<void()> slot) {
        return socket_->onRemoteClose(slot);
      }
    }  // namespace streams
  }  // namespace io
}  // namespace span
