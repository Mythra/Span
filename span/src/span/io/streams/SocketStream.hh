#ifndef SPAN_SRC_SPAN_IO_STREAMS_SOCKETSTREAM_HH_
#define SPAN_SRC_SPAN_IO_STREAMS_SOCKETSTREAM_HH_

#include <memory>

#include "span/io/streams/Stream.hh"
#include "span/third_party/slimsig/slimsig.hh"

namespace span {
  namespace io {
    class Socket;

    namespace streams {
      class SocketStream : public Stream {
      public:
        typedef std::shared_ptr<SocketStream> ptr;

        explicit SocketStream(std::shared_ptr<span::io::Socket> socket, bool own = true);

        bool supportsHalfClose() { return true; }
        bool supportsRead() { return true; }
        bool supportsWrite() { return true; }
        bool supportsCancel() { return true; }

        void close(CloseType type = BOTH);

        size_t read(Buffer *buff, size_t len);
        size_t read(void *buff, size_t len);
        void cancelRead();

        size_t write(const Buffer *buff, size_t len);
        size_t write(const void *buff, size_t len);
        void cancelWrite();

        slimsig::signal_t<void()>::connection onRemoteClose(const std::function<void()>);

        std::shared_ptr<span::io::Socket> socket() { return socket_; }

      private:
        std::shared_ptr<span::io::Socket> socket_;
        bool own_;
      };
    }  // namespace streams
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_STREAMS_SOCKETSTREAM_HH_
