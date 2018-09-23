#include "gtest/gtest.h"

#include <utility>

#include "span/fibers/WorkerPool.hh"
#include "span/io/streams/Pipe.hh"
#include "span/io/streams/Stream.hh"
#include "span/io/streams/Tls.hh"

namespace {
  static void test_accept(span::io::streams::TLSStream::ptr server) {
    server->accept();
    server->flush();
  }

  TEST(TlsStream, basic) {
    span::fibers::WorkerPool pool;
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();

    span::io::streams::TLSStream::ptr sslServer(new span::io::streams::TLSStream(pipe.first, false));
    span::io::streams::TLSStream::ptr sslClient(new span::io::streams::TLSStream(pipe.second, true));

    pool.schedule(std::bind(&test_accept, sslServer));
    sslClient->connect();
    pool.dispatch();

    span::io::streams::Stream::ptr server = sslServer, client = sslClient;

    char buff[6];
    buff[5] = '\0';
    client->write("hello");
    client->flush(false);
    ASSERT_EQ(server->read(&buff, 5), 5u);
    ASSERT_STREQ(buff, "hello");
    server->write("world");
    server->flush(false);
    ASSERT_EQ(client->read(&buff, 5), 5u);
    ASSERT_STREQ(buff, "world");
  }
}  /// namespace
