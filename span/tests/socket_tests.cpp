#include <iostream>
#include <memory>
#include <vector>

#include "gtest/gtest.h"

#include "span/Common.hh"
#include "span/Timer.hh"
#include "span/exceptions/Assert.hh"
#include "span/fibers/Fiber.hh"
#include "span/io/IOManager.hh"
#include "span/io/Socket.hh"

namespace {
  struct Connection {
    span::io::Socket::ptr connect;
    span::io::Socket::ptr listen;
    span::io::Socket::ptr accept;
    span::io::IPAddress::ptr address;
  };

  static void acceptOne(Connection *conns) {
    SPAN_ASSERT(conns->listen);
    conns->accept = conns->listen->accept();
  }

  Connection establishConn(span::io::IOManager *ioManager) {
    Connection result;
    std::vector<span::io::Address::ptr> addresses = span::io::Address::lookup("127.0.0.1");
    SPAN_ASSERT(!addresses.empty());
    result.address = std::dynamic_pointer_cast<span::io::IPAddress>(addresses.front());
    result.listen = result.address->createSocket(ioManager, SOCK_STREAM);
    unsigned int opt = 1;
    result.listen->setOption(SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int idx = 0;
    while (true) {
      try {
        // Random port > 1000
        result.address->port(rand() % 50000 + 1000);
        result.listen->bind(result.address);
        break;
      } catch (...) {}
      idx++;
      if (idx > 1000) {
        throw std::runtime_error("Couldn't bind to random port");
      }
    }
    result.listen->listen();
    result.connect = result.address->createSocket(ioManager, SOCK_STREAM);
    return result;
  }

  TEST(Socket, acceptTimeout) {
    span::io::IOManager ioManager(2, true);
    Connection conns = establishConn(&ioManager);
    conns.listen->receiveTimeout(2000);
    uint64 start = span::TimerManager::now();
    ASSERT_THROW(conns.listen->accept(), std::runtime_error);
    ASSERT_GT((span::TimerManager::now() - start), 2000);
    ASSERT_THROW(conns.listen->accept(), std::runtime_error);
  }

  TEST(Socket, receiveTimeout) {
    span::io::IOManager ioManager;
    Connection conns = establishConn(&ioManager);
    conns.connect->receiveTimeout(1000);
    ioManager.schedule(std::bind(&acceptOne, &conns));
    conns.connect->connect(conns.address);
    ioManager.dispatch();
    char buf;
    uint64 start = span::TimerManager::now();
    ASSERT_THROW(conns.connect->receive(&buf, 1), std::runtime_error);
    ASSERT_GT((span::TimerManager::now() - start), 1000);
    ASSERT_THROW(conns.connect->receive(&buf, 1), std::runtime_error);
  }

  TEST(Socket, sendTimeout) {
    span::io::IOManager ioManager;
    Connection conns = establishConn(&ioManager);
    conns.connect->sendTimeout(2000);
    ioManager.schedule(std::bind(&acceptOne, &conns));
    conns.connect->connect(conns.address);
    ioManager.dispatch();
    char buf[65536];
    memset(buf, 0, sizeof(buf));
    uint64 start = span::TimerManager::now();
    ASSERT_THROW(while (true) { conns.connect->send(buf, sizeof(buf)); }, std::runtime_error);
    ASSERT_GT((span::TimerManager::now() - start), 2000);
  }

  template<class Exception>
  static void testShutdownException(bool send, bool shutdown, bool otherEnd) {
    span::io::IOManager ioManager;
    Connection conns = establishConn(&ioManager);
    ioManager.schedule(std::bind(&acceptOne, &conns));
    conns.connect->connect(conns.address);
    ioManager.dispatch();

    span::io::Socket::ptr socketToClose = otherEnd ? conns.accept : conns.connect;
    if (shutdown) {
      socketToClose->shutdown(SHUT_RDWR);
    } else {
      socketToClose.reset();
    }

    if (send) {
      if (otherEnd) {
        try {
          conns.connect->sendTimeout(100);
          while (true) {
            ASSERT_EQ(conns.connect->send("abc", 3), 3u);
          }
        } catch (Exception) { }
      } else {
        ASSERT_THROW(conns.connect->send("abc", 3), std::runtime_error);
      }
    } else {
      unsigned char readBuf[3];
      if (otherEnd) {
        ASSERT_EQ(conns.connect->send("abc", 3), 0u);
      } else {
#if PLATFORM != PLATFORM_WIN32
        // Silly non-Windows letting you receive after you told it no more
        if (shutdown) {
          ASSERT_EQ(conns.connect->receive(readBuf, 3), 0u);
        } else
#endif
        {
          ASSERT_THROW(conns.connect->receive(readBuf, 3), std::runtime_error);
        }
      }
    }
  }

  TEST(Socket, sendAfterClose) {
    testShutdownException<std::runtime_error>(true, true, false);
  }

  TEST(Socket, receiveAfterClose) {
    testShutdownException<std::runtime_error>(false, true, false);
  }

  TEST(Socket, sendAfterCloseOtherSide) {
    testShutdownException<std::runtime_error>(true, true, true);
  }

  TEST(Address, formatIPv4Address) {
    auto addr = span::io::IPv4Address::create("127.0.0.1", 80);
    auto addr_two = span::io::IPv4Address(0x7f000001, 80);
    ASSERT_EQ(addr->to_string(), "127.0.0.1:80");
    ASSERT_EQ(addr_two.to_string(), "127.0.0.1:80");
    ASSERT_THROW(span::io::IPv4Address("garbage"), std::invalid_argument);
  }

  TEST(Address, formatIPv6Address) {
    auto addr_one = span::io::IPv6Address::create("::", 80);
    auto addr_two = span::io::IPv6Address::create("::1", 80);
    auto addr_three = span::io::IPv6Address::create("2001:470:1f05:273:20c:29ff:feb3:5ddf", 80);
    auto addr_four = span::io::IPv6Address::create("2001:470::273:20c:0:0:5ddf", 80);
    auto addr_five = span::io::IPv6Address::create("2001:470:0:0:273:20c::5ddf", 80);

    ASSERT_EQ(addr_one->to_string(), "[::]:80");
    ASSERT_EQ(addr_two->to_string(), "[::1]:80");
    ASSERT_EQ(addr_three->to_string(), "[2001:470:1f05:273:20c:29ff:feb3:5ddf]:80");
    ASSERT_EQ(addr_four->to_string(), "[2001:470::273:20c:0:0:5ddf]:80");
    ASSERT_EQ(addr_five->to_string(), "[2001:470::273:20c:0:5ddf]:80");

    ASSERT_THROW(span::io::IPv6Address("garbage"), std::invalid_argument);
  }

  static void cancelMe(span::io::Socket::ptr socks) {
    socks->cancelAccept();
  }

  TEST(Socket, cancelAccept) {
    span::io::IOManager ioManager;
    Connection conns = establishConn(&ioManager);

    // cancelMe will get run when this fiber yields because it would block.
    ioManager.schedule(std::bind(&cancelMe, conns.listen));
    ASSERT_THROW(conns.listen->accept(), std::runtime_error);
  }

  TEST(Socket, cancelSend) {
    span::io::IOManager ioManager;
    Connection conns = establishConn(&ioManager);
    ioManager.schedule(std::bind(&acceptOne, &conns));
    conns.connect->connect(conns.address);
    ioManager.dispatch();

    char *buf = new char[65536];
    memset(buf, 1, 65536);
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = 65536;
    conns.connect->cancelSend();

    ASSERT_THROW(while (conns.connect->send(iov.iov_base, 65536)) {}, std::runtime_error);
    ASSERT_THROW(conns.connect->send(&iov, 1), std::runtime_error);
  }

  TEST(Socket, cancelReceive) {
    span::io::IOManager ioManager;
    Connection conns = establishConn(&ioManager);
    ioManager.schedule(std::bind(&acceptOne, &conns));
    conns.connect->connect(conns.address);
    ioManager.dispatch();

    char buf[3];
    memset(buf, 1, 3);
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = 3;
    conns.connect->cancelReceive();

    ASSERT_THROW(conns.connect->receive(iov.iov_base, 3), std::runtime_error);
    ASSERT_THROW(conns.connect->receive(&iov, 1), std::runtime_error);
  }

  TEST(Socket, sendReceive) {
    span::io::IOManager ioManager;
    Connection conns = establishConn(&ioManager);
    ioManager.schedule(std::bind(&acceptOne, &conns));
    conns.connect->connect(conns.address);
    ioManager.dispatch();

    const char *sendbuf = "abcd";
    char receivebuf[5];
    memset(receivebuf, 0, 5);

    ASSERT_EQ(conns.connect->send(sendbuf, 1), 1u);
    ASSERT_EQ(conns.accept->receive(receivebuf, 1), 1u);
    ASSERT_EQ(receivebuf[0], 'a');

    receivebuf[0] = 0;
    iovec iov[2];
    iov[0].iov_base = const_cast<void *>(static_cast<const void *>(&sendbuf[0]));
    iov[1].iov_base = const_cast<void *>(static_cast<const void *>(&sendbuf[2]));
    iov[0].iov_len = 2;
    iov[1].iov_len = 2;

    ASSERT_EQ(conns.connect->send(iov, 2), 4u);

    iov[0].iov_base = &receivebuf[0];
    iov[1].iov_base = &receivebuf[2];

    ASSERT_EQ(conns.accept->receive(iov, 2), 4u);
    ASSERT_STREQ(sendbuf, const_cast<const char *>(receivebuf));
  }

#if PLATFORM != PLATFORM_WIN32
  TEST(Socket, exceedIOVMAX) {
    span::io::IOManager ioManager;
    Connection conns = establishConn(&ioManager);
    ioManager.schedule(std::bind(&acceptOne, &conns));
    conns.connect->connect(conns.address);
    ioManager.dispatch();

    const char * buf = "abcd";
    size_t n = IOV_MAX + 1, len = 4;
    iovec *iovs = new iovec[n];
    for (size_t idx = 0; idx < n; ++idx) {
      iovs[idx].iov_base = const_cast<void *>(static_cast<const void *>(buf));
      iovs[idx].iov_len = len;
    }
    size_t rc = conns.connect->send(iovs, n);
    SPAN_ASSERT(rc <= IOV_MAX * len);
    SPAN_ASSERT(rc >= 0);
  }
#endif

  static void toRunOnClose(bool *cloze) {
    *cloze = true;
  }

  TEST(Socket, eventOnRemoteShutdown) {
    span::io::IOManager ioManager;
    Connection conns = establishConn(&ioManager);
    ioManager.schedule(std::bind(&acceptOne, &conns));
    conns.connect->connect(conns.address);
    ioManager.dispatch();

    bool remoteClosed = false;
    conns.accept->onRemoteClose(std::bind(&toRunOnClose, &remoteClosed));
    conns.connect->shutdown();
    ioManager.dispatch();
    ASSERT_TRUE(remoteClosed);
  }

  TEST(Socket, eventOnRemoteReset) {
    span::io::IOManager ioManager;
    Connection conns = establishConn(&ioManager);
    ioManager.schedule(std::bind(&acceptOne, &conns));
    conns.connect->connect(conns.address);
    ioManager.dispatch();

    bool remoteClosed = false;
    conns.accept->onRemoteClose(std::bind(&toRunOnClose, &remoteClosed));
    conns.connect.reset();
    ioManager.dispatch();
    ASSERT_TRUE(remoteClosed);
  }
}  // namespace
