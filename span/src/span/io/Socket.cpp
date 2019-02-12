#include "span/io/Socket.hh"

#include "glog/logging.h"
#include "span/Common.hh"
#include "span/exceptions/Assert.hh"
#include "span/fibers/Fiber.hh"
#include "span/io/IOManager.hh"
#include "span/fibers/Scheduler.hh"
#include "span/Timer.hh"

#if PLATFORM != PLATFORM_WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>

#define closesocket close
#endif

#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace span {
  namespace io {
    namespace {
      enum Family {
        UNSPECIFIED = AF_UNSPEC,
        IP4 = AF_INET,
        IP6 = AF_INET6
      };

      enum Type {
        STREAM = SOCK_STREAM,
        DATAGRAM = SOCK_DGRAM
      };

      enum Protocol {
        ANY = 0,
        TCP = IPPROTO_TCP,
        UDP = IPPROTO_UDP
      };

      static inline bool isInterupted(int errnoVal) {
        switch (errnoVal) {
#if UNIX_FLAVOUR == UNIX_FLAVOUR_LINUX
          case ECANCELED:
#endif
          case EINTR:
            return true;
        }

        return false;
      }

      std::ostream &operator <<(std::ostream &os, Family family) {
        switch (family) {
          case UNSPECIFIED:
            return os << "AF_UNSPEC";
          case IP4:
            return os << "AF_INET";
          case IP6:
            return os << "AF_INET6";
          default:
            return os << static_cast<int>(family);
        }
      }

      std::ostream &operator <<(std::ostream &os, Type type) {
        switch (type) {
          case STREAM:
            return os << "SOCK_STREAM";
          case DATAGRAM:
            return os << "SOCK_DGRAM";
          default:
            return os << static_cast<int>(type);
        }
      }

      std::ostream &operator <<(std::ostream &os, Protocol protocol) {
        switch (protocol) {
          case TCP:
            return os << "IPPROTO_TCP";
          case UDP:
            return os << "IPPROTO_UDP";
          default:
            return os << static_cast<int>(protocol);
        }
      }
    }  // namespace

    static int g_iosPortIndex;

    namespace {
      static struct IOSInitializer {
        IOSInitializer() {
          g_iosPortIndex = std::ios_base::xalloc();
        }
      } g_iosInit;
    }  // namespace

    Socket::Socket(IOManager *ioManager, int family, int type, int protocol, int initialize) : sock_(-1),
      receiveTimeout_(~0ull),
      sendTimeout_(~0ull),
      family_(family),
      protocol_(protocol),
      ioManager_(ioManager),
      cancelledSend_(0),
      cancelledReceive_(0),
      isConnected_(false),
      isRegisteredForRemoteClose_(false) {
      // Windows accepts type == 0 as implying SOCK_STREAM; other OS's aren't so lenient.
      SPAN_ASSERT(type != 0);
    }

    Socket::Socket(int family, int type, int protocol) : sock_(-1),
      family_(family),
      protocol_(protocol),
      ioManager_(NULL),
      isConnected_(false),
      isRegisteredForRemoteClose_(false) {
      // Windows accepts type == 0 as implying SOCK_STREAM; other OS's aren't so lenient.
      SPAN_ASSERT(type != 0);

      sock_ = ::socket(family, type, protocol);
      DLOG(INFO) << this << " socket(" << static_cast<Family>(family) << ", " <<
        static_cast<Type>(type) << ", " << static_cast<Protocol>(protocol) << "): " << sock_;
      if (sock_ == -1) {
        throw std::runtime_error("socket");
      }

#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
      unsigned int opt = 1;
      if (setsockopt(sock_, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt)) == -1) {
        ::closesocket(sock_);
        throw std::runtime_error("setsockopt");
      }
#endif
    }

    Socket::Socket(IOManager *ioManager, int family, int type, int protocol) : sock_(-1),
      receiveTimeout_(~0ull),
      sendTimeout_(~0ull),
      family_(family),
      protocol_(protocol),
      ioManager_(ioManager),
      cancelledSend_(0),
      cancelledReceive_(0),
      isConnected_(false),
      isRegisteredForRemoteClose_(false) {
      // Windows accepts type == 0 as implying SOCK_STREAM; other OS's aren't so lenient.
      SPAN_ASSERT(type != 0);

      sock_ = ::socket(family, type, protocol);
      DLOG(INFO) << this << " socket(" << static_cast<Family>(family) << ", " <<
        static_cast<Type>(type) << ", " << static_cast<Protocol>(protocol) << "): " << sock_;

      if (sock_ == -1) {
        throw std::runtime_error("socket");
      }
#if PLATFORM != PLATFORM_WIN32
      if (fcntl(sock_, F_SETFL, O_NONBLOCK) == -1) {
        ::closesocket(sock_);
        throw std::runtime_error("fcntl");
      }
#endif
#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
      unsigned int opt = 1;
      if (setsockopt(sock_, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt)) == -1) {
        ::closesocket(sock_);
        throw std::runtime_error("setsockopt");
      }
#endif
    }

    Socket::~Socket() {
#if PLATFORM != PLATFORM_WIN32
      if (isRegisteredForRemoteClose_) {
        ioManager_->unregisterEvent(sock_, IOManager::CLOSE);
      }
#endif
      if (sock_ != -1) {
        int rc = ::closesocket(sock_);
        if (rc) {
          LOG(ERROR) << this << " close(" << sock_ << ")";
        } else {
          DLOG(INFO) << this << " close(" << sock_ << ")";
        }
      }
    }

    void Socket::bind(const Address &addr) {
      SPAN_ASSERT(addr.family() == family_);

      if (::bind(sock_, addr.name(), addr.nameLen())) {
        LOG(ERROR) << this << " bind(" << sock_ << ", " << addr;
        throw std::runtime_error("bind");
      }
      DLOG(INFO) << this << " bind(" << sock_ << ", " << addr << ")";
      localAddress();
    }

    void Socket::bind(Address::ptr addr) {
      bind(*addr);
    }

    void Socket::connect(const Address &to) {
      SPAN_ASSERT(to.family() == family_);
      if (!ioManager_) {
        if (::connect(sock_, to.name(), to.nameLen())) {
          LOG(ERROR) << this << " connect(" << sock_ << "," <<
            to << ")";
          throw std::runtime_error("connect");
        }
        DLOG(INFO) << this << " connect(" << sock_ << ", " << to << ") local: " << *(localAddress());
      } else {
#if PLATFORM != PLATFORM_WIN32
        if (!::connect(sock_, to.name(), to.nameLen())) {
          DLOG(INFO) << this << " connect(" << sock_ << ", " << to << ") local: " << *(localAddress());
          // Worked first time
          return;
        }
        if (lastError() == EINPROGRESS) {
          ioManager_->registerEvent(sock_, IOManager::WRITE);
          if (cancelledSend_) {
            LOG(ERROR) << this << " connect(" << sock_ << ", " << to << "): (" << cancelledSend_ << ")";
            ioManager_->cancelEvent(sock_, IOManager::WRITE);
            ::span::fibers::Scheduler::yieldTo();
            throw std::runtime_error("connect cancelledSend_");
          }

          Timer::ptr timeout;
          if (sendTimeout_ != ~0ull) {
            timeout = ioManager_->registerTimer(
              sendTimeout_,
              std::bind(
                &Socket::cancelIo,
                this,
                IOManager::WRITE,
                &cancelledSend_,
                ETIMEDOUT));
          }
          ::span::fibers::Scheduler::yieldTo();
          if (timeout) {
            timeout->cancel();
          }
          if (cancelledSend_) {
            LOG(ERROR) << this << " connect(" << sock_ << ", " << to << "): (" << cancelledSend_ << ")";
            throw std::runtime_error("connect cancelledSend_");
          }

          int err;
          size_t size = sizeof(int);
          getOption(SOL_SOCKET, SO_ERROR, &err, &size);
          if (err != 0) {
            LOG(ERROR) << this << " connect(" << sock_ << ", " << to << "): (" << err << ")";
            throw std::runtime_error("conect");
          }
          DLOG(INFO) << this << " connect(" << sock_ << ", " << to << ") local: " << *(localAddress());
        } else {
          LOG(ERROR) << this << " connect(" << sock_ << ", " << to << "): (" << lastError() << ")";
          throw std::runtime_error("connect");
        }
#endif

        isConnected_ = true;
        if (!onRemoteClose_.empty()) {
          registerForRemoteClose();
        }
      }
    }

    void Socket::listen(int backlog) {
      int rc = ::listen(sock_, backlog);
      if (rc) {
        LOG(ERROR) << this << " listen(" << sock_ << ", " << backlog << "): " << rc << "(" << lastError() << ")";
        throw std::runtime_error("listen");
      }
      DLOG(INFO) << this << " listen(" << sock_ << ", " << backlog << "): " << rc << " (" << lastError() << ")";
    }

    Socket::ptr Socket::accept() {
      Socket::ptr sock(new Socket(ioManager_, family_, type(), protocol_, 0));
      accept(sock);
      return sock;
    }

    void Socket::accept(Socket::ptr target) {
#if PLATFORM != PLATFORM_WIN32
      SPAN_ASSERT(target->sock_ == -1);
#endif
      SPAN_ASSERT(target->family_ == family_);
      SPAN_ASSERT(target->protocol_ == protocol_);

      if (!ioManager_) {
        socket_t newsock = ::accept(sock_, NULL, NULL);
        if (newsock == -1) {
          LOG(ERROR) << this << " accept(" << sock_ << "): " << newsock << " (" << lastError() << ")";
          throw std::runtime_error("accept");
        }
        target->sock_ = newsock;
        DLOG(INFO) << this << " accept(" << sock_ << "): " << newsock << " (" << target->remoteAddress() << ", "  <<
          &target << ")";
      } else {
#if PLATFORM != PLATFORM_WIN32
        int newsock;
        error_t error;
        do {
          newsock = ::accept(sock_, NULL, NULL);
          error = errno;
        } while (newsock == -1 && isInterupted(error));

        while (newsock == -1 && error == EAGAIN) {
          ioManager_->registerEvent(sock_, IOManager::READ);
          if (cancelledReceive_) {
            LOG(ERROR) << this << " accept(" << sock_ << "): (" << cancelledReceive_ << ")";
            ioManager_->cancelEvent(sock_, IOManager::READ);
            ::span::fibers::Scheduler::yieldTo();
            throw std::runtime_error("accept");
          }

          Timer::ptr timeout;
          if (receiveTimeout_ != ~0ull) {
            timeout = ioManager_->registerTimer(
              receiveTimeout_,
              std::bind(
                &Socket::cancelIo,
                this,
                IOManager::READ,
                &cancelledReceive_,
                ETIMEDOUT));
          }
          ::span::fibers::Scheduler::yieldTo();
          if (timeout) {
            timeout->cancel();
          }
          if (cancelledReceive_) {
            LOG(ERROR) << this << " accept(" << sock_ << "): (" << cancelledReceive_ << ")";
            throw std::runtime_error("accept");
          }

          do {
            newsock = ::accept(sock_, NULL, NULL);
            error = lastError();
          } while (newsock == -1 && isInterupted(error));
        }

        if (newsock == -1) {
          LOG(ERROR) << this << " accept(" << sock_ << "): " << newsock << " (" << error << ")";
          throw std::runtime_error("accept");
        }

        if (fcntl(newsock, F_SETFL, O_NONBLOCK) == -1) {
          ::close(newsock);
          throw std::runtime_error("fcntl");
        }

        target->sock_ = newsock;
        DLOG(INFO) << this << " accept(" << sock_ << "): " << newsock << " (" << *(target->remoteAddress()) << ", " <<
          &target << ")";
#endif
        target->isConnected_ = true;
        if (!target->onRemoteClose_.empty()) {
          target->registerForRemoteClose();
        }
      }
    }

    void Socket::shutdown(int how) {
      if (::shutdown(sock_, how)) {
        LOG(ERROR) << this << " shutdown(" << sock_ << "," << how << "): (" << lastError() << ")";
        throw std::runtime_error("shutdown");
      }
      if (isRegisteredForRemoteClose_) {
#if PLATFORM != PLATFORM_WIN32
        ioManager_->unregisterEvent(sock_, IOManager::CLOSE);
#endif
        isRegisteredForRemoteClose_ = false;
      }
      isConnected_ = false;
      DLOG(INFO) << this << " shutdown(" << sock_ << ", " << how << ")";
    }

#define SPAN_SOCKET_LOG(result, error) \
  if (result == -1) { \
    if (isSend && address) { \
      LOG(ERROR) << this << " " << api << "(" << sock_ << ", " <<  \
        len << ", " << *address << "): " << "(" << error << ")"; \
    } else { \
      LOG(ERROR) << this << " " << api << "(" << sock_ << ", " << len << "): " << "(" << error << ")"; \
    } \
  } else { \
    if (isSend && address) { \
      DLOG(INFO) << this << " " << api << "(" << sock_ << ", " << len << ", " << *address << "): " << result; \
    } else if (!isSend && address) { \
      DLOG(INFO) << this << " " << api << "(" << sock_ << ", " << len << "): " << result << ", " << *address; \
    } else { \
      DLOG(INFO) << this << " " << api << "(" << sock_ << ", " << len << "): " << result; \
    } \
  }

    template<bool isSend>
    size_t Socket::doIO(iovec *buffers, size_t len, int *flags, Address *address) {
#if PLATFORM == PLATFORM_UNIX && UNIX_FLAVOUR != UNIX_FLAVOUR_OSX
      *flags |= MSG_NOSIGNAL;
#endif

#if PLATFORM != PLATFORM_WIN32
      const char *api = isSend ? "sendmsg" : "recvmsg";
#endif
      error_t &cancelled = isSend ? cancelledSend_ : cancelledReceive_;
      uint64 &timeout = isSend ? sendTimeout_ : receiveTimeout_;

#if PLATFORM != PLATFORM_WIN32
      msghdr msg;
      memset(&msg, 0, sizeof(msghdr));
      msg.msg_iov = buffers;
      msg.msg_iovlen = std::min(len, static_cast<size_t>(IOV_MAX));
      if (address) {
        msg.msg_name = static_cast<sockaddr *>(address->name());
        msg.msg_namelen = address->nameLen();
      }
      IOManager::Event event = isSend ? IOManager::WRITE : IOManager::READ;
      if (ioManager_) {
        if (cancelled) {
          SPAN_SOCKET_LOG(-1, cancelled);
          throw std::runtime_error(api);
        }
      }
      int rc;
      error_t error;

      do {
        rc = isSend ? sendmsg(sock_, &msg, *flags) : recvmsg(sock_, &msg, *flags);
        error = lastError();
      } while (rc == -1 && isInterupted(error));

      while (ioManager_ && rc == -1 && error == EAGAIN) {
        ioManager_->registerEvent(sock_, event);
        ::span::Timer::ptr timer;
        if (timeout != ~0ull) {
          timer = ioManager_->registerTimer(
            timeout,
            std::bind(&Socket::cancelIo, this, event, &cancelled, ETIMEDOUT));
        }
        ::span::fibers::Scheduler::yieldTo();
        if (timer) {
          timer->cancel();
        }
        if (cancelled) {
          SPAN_SOCKET_LOG(-1, cancelled);
          throw std::runtime_error(api);
        }

        do {
          rc = isSend ? sendmsg(sock_, &msg, *flags) : recvmsg(sock_, &msg, *flags);
          error = lastError();
        } while (rc == -1 && isInterupted(error));
      }
      SPAN_SOCKET_LOG(rc, error);
      if (rc == -1) {
        throw std::runtime_error(api);
      }
      if (!isSend) {
        flags = &msg.msg_flags;
      }
      return rc;
#endif
    }

    size_t Socket::send(const void *buffer, size_t len, int flags) {
      iovec buffers;
      buffers.iov_base = const_cast<void *>(buffer);
      buffers.iov_len = static_cast<u_long>(std::min<size_t>(len, 0xFFFFFFFF));
      return doIO<true>(&buffers, 1, &flags);
    }

    size_t Socket::send(const iovec *buffers, size_t len, int flags) {
      return doIO<true>(const_cast<iovec *>(buffers), len, &flags);
    }

    size_t Socket::sendTo(const void *buffer, size_t len, int flags, const Address &to) {
      iovec buffers;
      buffers.iov_base = const_cast<void *>(buffer);
      buffers.iov_len = static_cast<u_long>(std::min<size_t>(len, 0xFFFFFFFF));
      return doIO<true>(&buffers, 1, &flags, const_cast<Address *>(&to));
    }

    size_t Socket::sendTo(const iovec *buffers, size_t len, int flags, const Address &to) {
      return doIO<true>(const_cast<iovec *>(buffers), len, &flags, const_cast<Address *>(&to));
    }

    size_t Socket::receive(void *buffer, size_t len, int *flags) {
      iovec buffers;
      buffers.iov_base = buffer;
      buffers.iov_len = static_cast<u_long>(std::min<size_t>(len, 0xFFFFFFFF));
      int flagStorage = 0;
      if (!flags) {
        flags = &flagStorage;
      }
      return doIO<false>(&buffers, len, flags);
    }

    size_t Socket::receive(iovec *buffers, size_t len, int *flags) {
      int flagStorage = 0;
      if (!flags) {
        flags = &flagStorage;
      }
      return doIO<false>(buffers, len, flags);
    }

    size_t Socket::receiveFrom(void *buffer, size_t len, Address *from, int *flags) {
      iovec buffers;
      buffers.iov_base = buffer;
      buffers.iov_len = static_cast<u_long>(std::min<size_t>(len, 0xFFFFFFFF));
      int flagStorage = 0;
      if (!flags) {
        flags = &flagStorage;
      }
      return doIO<false>(&buffers, 1, flags, from);
    }

    size_t Socket::receiveFrom(iovec *buffers, size_t len, Address *from, int *flags) {
      int flagStorage = 0;
      if (!flags) {
        flags = &flagStorage;
      }
      return doIO<false>(buffers, len, flags, from);
    }

    void Socket::getOption(int level, int option, void *result, size_t *len) {
      int ret = getsockopt(sock_, level, option, static_cast<char *>(result), reinterpret_cast<socklen_t *>(len));
      if (ret) {
        throw std::runtime_error("getsockopt");
      }
    }

    void Socket::setOption(int level, int option, const void *value, size_t len) {
      if (setsockopt(sock_, level, option, static_cast<const char*>(value), static_cast<socklen_t>(len))) {
        error_t error = lastError();
        LOG(ERROR) << this << " setsockopt(" << sock_ << ", " << level << ", " << option << "): (" << error << ")";
        throw std::runtime_error("setsockopt");
      }
      DLOG(INFO) << this << " setsockopt(" << sock_ << ", " << level << ", " << option << "): 0";
    }

    void Socket::cancelAccept() {
        SPAN_ASSERT(ioManager_);

#if PLATFORM != PLATFORM_WIN32
        cancelIo(IOManager::READ, &cancelledReceive_, ECANCELED);
#endif
    }

    void Socket::cancelConnect() {
        SPAN_ASSERT(ioManager_);

#if PLATFORM != PLATFORM_WIN32
        cancelIo(IOManager::WRITE, &cancelledSend_, ECANCELED);
#endif
    }

    void Socket::cancelSend() {
        SPAN_ASSERT(ioManager_);

#if PLATFORM != PLATFORM_WIN32
        cancelIo(IOManager::WRITE, &cancelledSend_, ECANCELED);
#endif
    }

    void Socket::cancelReceive() {
      SPAN_ASSERT(ioManager_);

#if PLATFORM != PLATFORM_WIN32
      cancelIo(IOManager::READ, &cancelledReceive_, ECANCELED);
#endif
    }

#if PLATFORM != PLATFORM_WIN32
    void Socket::cancelIo(int event, error_t *cancelled, error_t error) {
      SPAN_ASSERT(error);
      if (*cancelled) {
          return;
      }
      LOG(WARNING) << this << ((event == IOManager::READ) ? " cancelReceive(" : "cancelSend(") << sock_ << ")";
      *cancelled = error;
      ioManager_->cancelEvent(sock_, static_cast<IOManager::Event>(event));
    }
#endif

    Address::ptr Socket::emptyAddress() {
        switch (family_) {
        case AF_INET:
            return Address::ptr(new IPv4Address());
        case AF_INET6:
            return Address::ptr(new IPv6Address());
        default:
            return Address::ptr(new UnknownAddress(family_));
        }
    }

    Address::ptr Socket::remoteAddress() {
        if (remoteAddress_) {
            return remoteAddress_;
        }
        Address::ptr result;
        switch (family_) {
        case AF_INET:
          result.reset(new IPv4Address());
          break;
        case AF_INET6:
          result.reset(new IPv6Address());
          break;
#if PLATFORM != PLATFORM_WIN32
        case AF_UNIX:
          result.reset(new UnixAddress());
          break;
#endif
        default:
          result.reset(new UnknownAddress(family_));
          break;
        }
        socklen_t namelen = result->nameLen();
        if (getpeername(sock_, result->name(), &namelen)) {
          throw std::runtime_error("getpeername");
        }
        SPAN_ASSERT(namelen <= result->nameLen());
#if PLATFORM != PLATFORM_WIN32
        if (family_ == AF_UNIX) {
          std::shared_ptr<UnixAddress> addr = std::dynamic_pointer_cast<UnixAddress>(result);
          addr->nameLen(namelen);
        }
#endif
      return remoteAddress_ = result;
    }

    Address::ptr Socket::localAddress() {
      if (localAddress_) {
        return localAddress_;
      }
      Address::ptr result;
      switch (family_) {
      case AF_INET:
        result.reset(new IPv4Address());
        break;
      case AF_INET6:
        result.reset(new IPv6Address());
        break;
#if PLATFORM != PLATFORM_WIN32
      case AF_UNIX:
        result.reset(new UnixAddress());
        break;
#endif
      default:
        result.reset(new UnknownAddress(family_));
        break;
      }
      socklen_t namelen = result->nameLen();
      if (getsockname(sock_, result->name(), &namelen)) {
        throw std::runtime_error("getsockname");
      }
      SPAN_ASSERT(namelen <= result->nameLen());
#if PLATFORM != PLATFORM_WIN32
      if (family_ == AF_UNIX) {
        std::shared_ptr<UnixAddress> addr = std::dynamic_pointer_cast<UnixAddress>(result);
        addr->nameLen(namelen);
      }
#endif
      return localAddress_ = result;
    }

    int Socket::type() {
      int result;
      size_t len = sizeof(int);
      getOption(SOL_SOCKET, SO_TYPE, &result, &len);
      return result;
    }

    slimsig::signal_t<void()>::connection Socket::onRemoteClose(const std::function<void()> slot) {
      slimsig::signal_t<void()>::connection result = onRemoteClose_.connect(slot);
      if (isConnected_ && !isRegisteredForRemoteClose_) {
        registerForRemoteClose();
      }
      return result;
    }

    void Socket::callOnRemoteClose(Socket *self) {
      if (self) {
        self->onRemoteClose_.emit();
      }
    }

    void Socket::registerForRemoteClose() {
#if PLATFORM != PLATFORM_WIN32
      ioManager_->registerEvent(sock_, IOManager::CLOSE, std::bind(
        &Socket::callOnRemoteClose, this));
#endif
      isRegisteredForRemoteClose_ = true;
    }

    std::vector<Address::ptr> Address::lookup(const std::string &host, int family, int type, int protocol) {
#if PLATFORM != PLATFORM_WIN32
      addrinfo hints, *results, *next;
#endif
      hints.ai_flags = 0;
      hints.ai_family = family;
      hints.ai_socktype = type;
      hints.ai_protocol = protocol;
      hints.ai_addrlen = 0;
      hints.ai_canonname = NULL;
      hints.ai_addr = NULL;
      hints.ai_next = NULL;
      std::string node;
      const char *service = NULL;

      // Check for [ipv6addr] (with optional :service)
      if (!host.empty() && host[0] == '[') {
        const char *endipv6 = static_cast<const char*>(memchr(host.c_str() + 1, ']', host.size() - 1));
        if (endipv6) {
          if ((*endipv6 + 1) == ':') {
            service = endipv6 + 2;
          }
          node = host.substr(1, endipv6 - host.c_str() - 1);
        }
      }

      // Check for node:service
      if (node.empty()) {
        service = static_cast<const char*>(memchr(host.c_str(), ':', host.size()));
        if (service) {
          // More than 1 : means it's not node:service
          if (!memchr(service + 1, ':', host.c_str() + host.size() - service - 1)) {
            node = host.substr(0, service - host.c_str());
            ++service;
          } else {
            service = NULL;
          }
        }
      }

      if (node.empty()) {
        node = host;
      }

      int error;
#if PLATFORM != PLATFORM_WIN32
      error = getaddrinfo(node.c_str(), service, &hints, &results);
#endif
      if (error) {
        LOG(ERROR) << "getaddrinfo(" << host << ", " << static_cast<Family>(family) << ", " <<
          static_cast<Type>(type) << "): (" << error << ")";
        throw std::runtime_error("getaddrinfo");
      }

      std::vector<Address::ptr> result;
      next = results;
      while (next) {
        result.push_back(create(next->ai_addr, static_cast<socklen_t>(next->ai_addrlen)));
        next = next->ai_next;
      }
#if PLATFORM != PLATFORM_WIN32
      freeaddrinfo(results);
#endif
      return result;
    }

    template<class T>
    static unsigned int countBits(T value) {
      unsigned int result = 0;
      // See: http://www.graphics.stanford.edu/~seander/bithacks.html#CountBitsSetKernighan
      for (; value; ++result) {
        value &= value - 1;
      }
      return result;
    }

    std::multimap<std::string, std::pair<Address::ptr, unsigned int>> Address::getInterfaceAddresses(int family) {
      std::multimap<std::string, std::pair<Address::ptr, unsigned int>> result;

#if PLATFORM != PLATFORM_WIN32
      struct ifaddrs *next, *results;

      if (getifaddrs(&results) != 0) {
        throw std::runtime_error("getifaddrs");
      }
      try {
        for (next = results; next; next = next->ifa_next) {
          Address::ptr address, baddress;
          unsigned int prefixLen = ~0u;

          // some unusual interfaces (like teql0) show up, but don't have
          // but don't have any addresses associated with them.
          if (!next->ifa_addr) {
            continue;
          }
          if (family != AF_UNSPEC && family != next->ifa_addr->sa_family) {
            continue;
          }

          switch (next->ifa_addr->sa_family) {
          case AF_INET:
            {
              address = create(next->ifa_addr, sizeof(sockaddr_in));
              unsigned int netmask = (reinterpret_cast<sockaddr_in *>(next->ifa_netmask))->sin_addr.s_addr;
              prefixLen = countBits(netmask);
              break;
            }
          case AF_INET6:
            {
              address = create(next->ifa_addr, sizeof(sockaddr_in6));
              prefixLen = 0;
              in6_addr &netmask = (reinterpret_cast<sockaddr_in6 *>(next->ifa_netmask))->sin6_addr;
              for (size_t idx = 0; idx < 16; ++idx) {
                prefixLen += countBits(netmask.s6_addr[idx]);
              }
              break;
            }
          default:
            break;
          }

          if (address) {
            result.insert(std::make_pair(next->ifa_name, std::make_pair(address, prefixLen)));
          }
        }
      } catch (...) {
        freeifaddrs(results);
        throw;
      }
      freeifaddrs(results);
      return result;
#endif
    }

    std::vector<std::pair<Address::ptr, unsigned int>> Address::getInterfaceAddresses(
      const std::string &iface,
      int family
    ) {
      std::vector<std::pair<Address::ptr, unsigned int>> result;
      if (iface.empty() || iface == "*") {
        if (family == AF_INET || family == AF_UNSPEC) {
          result.push_back(std::make_pair(Address::ptr(new IPv4Address()), 0u));
        }
        if (family == AF_INET6 || family == AF_UNSPEC) {
          result.push_back(std::make_pair(Address::ptr(new IPv6Address()), 0u));
        }
        return result;
      }
      typedef std::multimap<std::string, std::pair<Address::ptr, unsigned int>> AddressesMap;
      AddressesMap interfaces = getInterfaceAddresses(family);
      std::pair<AddressesMap::iterator, AddressesMap::iterator> its = interfaces.equal_range(iface);
      for (; its.first != its.second; ++its.first) {
        result.push_back(its.first->second);
      }
      return result;
    }

    Address::ptr Address::create(const sockaddr *name, socklen_t nameLen) {
      SPAN_ASSERT(name);
      Address::ptr result;
      switch (name->sa_family) {
      case AF_INET:
        result.reset(new IPv4Address());
        SPAN_ASSERT(nameLen <= result->nameLen());
        memcpy(result->name(), name, nameLen);
        break;
      case AF_INET6:
        result.reset(new IPv6Address());
        SPAN_ASSERT(nameLen <= result->nameLen());
        memcpy(result->name(), name, nameLen);
        break;
      default:
        result.reset(new UnknownAddress(name->sa_family));
        SPAN_ASSERT(nameLen <= result->nameLen());
        memcpy(result->name(), name, nameLen);
        break;
      }

      return result;
    }

    Address::ptr Address::clone() {
      return create(name(), nameLen());
    }

    Socket::ptr Address::createSocket(int type, int protocol) {
      return Socket::ptr(new Socket(family(), type, protocol));
    }

    Socket::ptr Address::createSocket(IOManager *ioManager, int type, int protocol) {
      return Socket::ptr(new Socket(ioManager, family(), type, protocol));
    }

    std::ostream & Address::insert(std::ostream &os) const {
      return os << "(Unknown addr " << family() << ")";
    }

    std::string Address::to_string() const {
      std::ostringstream stream;
      insert(stream);
      return stream.str();
    }

    bool Address::operator<(const Address &rhs) const {
      socklen_t minimum = std::min(nameLen(), rhs.nameLen());
      int result = memcmp(name(), rhs.name(), minimum);
      if (result < 0) {
        return true;
      } else if (result > 0) {
        return false;
      }
      if (nameLen() < rhs.nameLen()) {
        return true;
      }
      return false;
    }

    bool Address::operator==(const Address &rhs) const {
      return nameLen() == rhs.nameLen() && memcmp(name(), rhs.name(), nameLen()) == 0;
    }

    bool Address::operator!=(const Address &rhs) const {
      return !(*this == rhs);
    }

    std::vector<IPAddress::ptr> IPAddress::lookup(
      const std::string &host,
      int family,
      int type,
      int protocol,
      int port
    ) {
      std::vector<Address::ptr> addrResult = Address::lookup(host, family, type, protocol);
      std::vector<ptr> result;
      result.reserve(addrResult.size());
      for (std::vector<Address::ptr>::const_iterator it(addrResult.begin()); it != addrResult.end(); ++it) {
        ptr addr = std::dynamic_pointer_cast<IPAddress>(*it);
        if (addr) {
          if (port >= 0) {
            addr->port(port);
          }
          result.push_back(addr);
        }
      }

      return result;
    }

    IPAddress::ptr IPAddress::clone() {
      return std::static_pointer_cast<IPAddress>(Address::clone());
    }

    IPAddress::ptr IPAddress::create(const char *address, uint16 port) {
      addrinfo hints, *results;
      memset(&hints, 0, sizeof(addrinfo));
      hints.ai_flags = AI_NUMERICHOST;
      hints.ai_family = AF_UNSPEC;
      int error = getaddrinfo(address, NULL, &hints, &results);
      if (error == EAI_NONAME) {
        throw std::invalid_argument("address");
      } else if (error) {
        LOG(ERROR) << "getaddrinfo(" << address << ", AI_NUMERICHOST): (" << error << ")";
        throw std::runtime_error("getaddrinfo");
      }
      try {
        IPAddress::ptr result = std::static_pointer_cast<IPAddress>(
          Address::create(results->ai_addr, static_cast<socklen_t>(results->ai_addrlen)));
        result->port(port);
        freeaddrinfo(results);
        return result;
      } catch (...) {
        freeaddrinfo(results);
        throw;
      }
    }

    IPv4Address::IPv4Address(unsigned int address, uint16 port) {
      memset(&sin, 0, sizeof(sin));
      sin.sin_family = AF_INET;
      sin.sin_port = htons(port);
      sin.sin_addr.s_addr = htonl(address);
    }

#if PLATFORM != PLATFORM_WIN32
    static int pinet_pton(int af, const char *src, void *dst) {
      return inet_pton(af, src, dst);
    }
#endif

    IPv4Address::IPv4Address(const char *address, uint16 port) {
      memset(&sin, 0, sizeof(sin));
      sin.sin_family = AF_INET;
      sin.sin_port = htons(port);
      int result = pinet_pton(AF_INET, address, &sin.sin_addr);
      if (result == 0) {
        throw std::invalid_argument("address");
      }
      if (result < 0) {
        throw std::runtime_error("inet_pton");
      }
    }

    template<class T>
    static T createMask(unsigned int bits) {
      SPAN_ASSERT(bits <= sizeof(T) * 8);
      return (1 << (sizeof(T) * 8 - bits)) - 1;
    }

    IPv4Address::ptr IPv4Address::broadcastAddress(unsigned int prefixLen) {
      SPAN_ASSERT(prefixLen <= 32);
      sockaddr_in baddr(sin);
      baddr.sin_addr.s_addr |= htonl(createMask<unsigned int>(prefixLen));
      return std::static_pointer_cast<IPv4Address>(Address::create(
        reinterpret_cast<const sockaddr *>(&baddr), sizeof(sockaddr_in)));
    }

    IPv4Address::ptr IPv4Address::networkAddress(unsigned int prefixLen) {
      SPAN_ASSERT(prefixLen <= 32);
      sockaddr_in baddr(sin);
      baddr.sin_addr.s_addr &= htonl(~createMask<unsigned int>(prefixLen));
      return std::static_pointer_cast<IPv4Address>(Address::create(
        reinterpret_cast<const sockaddr *>(&baddr),
        sizeof(sockaddr_in)));
    }

    IPv4Address::ptr IPv4Address::createSubnetMask(unsigned int prefixLen) {
      SPAN_ASSERT(prefixLen <= 32);
      sockaddr_in subnet;
      memset(&subnet, 0, sizeof(sockaddr_in));
      subnet.sin_family = AF_INET;
      subnet.sin_addr.s_addr = htonl(~createMask<unsigned int>(prefixLen));
      return std::static_pointer_cast<IPv4Address>(Address::create(
        reinterpret_cast<const sockaddr *>(&subnet),
        sizeof(sockaddr_in)));
    }

    std::ostream & IPv4Address::insert(std::ostream &os) const {
      int addr = htonl(sin.sin_addr.s_addr);
      os << ((addr >> 24) & 0xFF) << '.' << ((addr >> 16) & 0xFF) << '.' << ((addr >> 8) & 0xFF) << '.' <<
        (addr & 0xFF);
      if (!os.iword(g_iosPortIndex)) {
        os << ':' << htons(sin.sin_port);
      }
      return os;
    }

    std::string IPv4Address::to_string() const {
      std::ostringstream ostream;
      insert(ostream);
      return ostream.str();
    }

    IPv6Address::IPv6Address() {
      memset(&sin, 0, sizeof(sockaddr_in6));
      sin.sin6_family = AF_INET6;
    }

    IPv6Address::IPv6Address(const unsigned char address[16], uint16 port) {
      memset(&sin, 0, sizeof(sockaddr_in6));
      sin.sin6_family = AF_INET6;
      sin.sin6_port = htons(port);
      memcpy(&sin.sin6_addr.s6_addr, address, 16);
    }

    IPv6Address::IPv6Address(const char *address, uint16 port) {
      memset(&sin, 0, sizeof(sockaddr_in6));
      sin.sin6_family = AF_INET6;
      sin.sin6_port = htons(port);
      int result = pinet_pton(AF_INET6, address, &sin.sin6_addr);
      if (result == 0) {
        throw std::invalid_argument("address");
      }
      if (result < 0) {
        throw std::runtime_error("inet_pton");
      }
    }

    IPv6Address::ptr IPv6Address::broadcastAddress(unsigned int prefixLen) {
      SPAN_ASSERT(prefixLen <= 128);
      sockaddr_in6 baddr(sin);
      baddr.sin6_addr.s6_addr[prefixLen / 8] |= createMask<unsigned char>(prefixLen % 8);
      for (unsigned int idx = prefixLen / 8 + 1; idx < 16; ++idx) {
        baddr.sin6_addr.s6_addr[idx] = 0xFFU;
      }
      return std::static_pointer_cast<IPv6Address>(Address::create(
        reinterpret_cast<const sockaddr *>(&baddr),
        sizeof(sockaddr_in6)));
    }

    IPv6Address::ptr IPv6Address::networkAddress(unsigned int prefixLen) {
      SPAN_ASSERT(prefixLen <= 128);
      sockaddr_in6 baddr(sin);
      baddr.sin6_addr.s6_addr[prefixLen / 8] &= ~createMask<unsigned char>(prefixLen % 8);
      for (unsigned int idx = prefixLen / 8 + 1; idx < 16; ++idx) {
        baddr.sin6_addr.s6_addr[idx] = 0x00U;
      }
      return std::static_pointer_cast<IPv6Address>(Address::create(
        reinterpret_cast<const sockaddr *>(&baddr),
        sizeof(sockaddr_in6)));
    }

    IPv6Address::ptr IPv6Address::createSubnetMask(unsigned int prefixLen) {
      SPAN_ASSERT(prefixLen <= 128);
      sockaddr_in6 subnet;
      memset(&subnet, 0, sizeof(sockaddr_in6));
      subnet.sin6_family = AF_INET6;
      subnet.sin6_addr.s6_addr[prefixLen / 8] = ~createMask<unsigned char>(prefixLen % 8);
      for (unsigned int idx = 0; idx < prefixLen / 8; ++idx) {
        subnet.sin6_addr.s6_addr[idx] = 0xFFU;
      }
      return std::static_pointer_cast<IPv6Address>(Address::create(
        reinterpret_cast<const sockaddr*>(&subnet),
        sizeof(sockaddr_in6)));
    }

    std::ostream & IPv6Address::insert(std::ostream &os) const {
      std::ios_base::fmtflags flags = os.setf(std::ios_base::hex, std::ios_base::basefield);
      bool includePort = !os.iword(g_iosPortIndex);
      if (includePort) {
        os << '[';
      }
      uint16 *addr = const_cast<uint16 *>(reinterpret_cast<const uint16*>(
        sin.sin6_addr.s6_addr));
      bool usedZeros = false;
      for (size_t idx = 0; idx < 8; ++idx) {
        if (addr[idx] == 0 && !usedZeros) {
          continue;
        }
        if (idx != 0 && addr[idx - 1] == 0 && !usedZeros) {
          os << ':';
          usedZeros = true;
        }
        if (idx != 0) {
          os << ':';
        }
        os << static_cast<int>(htons(addr[idx]));
      }

      if (!usedZeros && addr[7] == 0) {
        os << "::";
      }

      if (includePort) {
        os << "]:" << std::dec << static_cast<int>(htons(sin.sin6_port));
      }

      os.setf(flags, std::ios_base::basefield);
      return os;
    }

    std::string IPv6Address::to_string() const {
      std::ostringstream ostream;
      insert(ostream);
      return ostream.str();
    }

#if PLATFORM != PLATFORM_WIN32
    const size_t UnixAddress::MAX_PATH_LEN = sizeof((static_cast<sockaddr_un *>(0))->sun_path) - 1;

    UnixAddress::UnixAddress(const std::string &path) {
      sun.sun_family = AF_UNIX;
      len = path.size() + 1;
#if UNIX_FLAVOUR == UNIX_FLAVOUR_LINUX
      // Abstract namespace; leading NULL, but no trailing NULL
      if (!path.empty() && path[0] == '\0') {
        --len;
      }
#endif
      SPAN_ASSERT(len <= sizeof(sun.sun_path));
      memcpy(sun.sun_path, path.c_str(), len);
      len += offsetof(sockaddr_un, sun_path);
    }

    UnixAddress::UnixAddress() {
      memset(&sun, 0, sizeof(sun));
      sun.sun_family = AF_UNIX;
      len = offsetof(sockaddr_un, sun_path) + MAX_PATH_LEN;
    }

    std::ostream & UnixAddress::insert(std::ostream &os) const {
#if UNIX_FLAVOUR == UNIX_FLAVOUR_LINUX
      if (len > offsetof(sockaddr_un, sun_path) && sun.sun_path[0] == '\0') {
        return os << "\\0" << std::string(sun.sun_path + 1, len - offsetof(sockaddr_un, sun_path) - 1);
      }
#endif
      return os << sun.sun_path;
    }

    std::string UnixAddress::to_string() const {
      std::ostringstream ostream;
      insert(ostream);
      return ostream.str();
    }
#endif

    UnknownAddress::UnknownAddress(int family) {
      sa.sa_family = family;
    }

    std::ostream &operator <<(std::ostream &os, const Address &addr) {
      return addr.insert(os);
    }

    bool operator <(const Address::ptr &lhs, const Address::ptr &rhs) {
      if (!lhs || !rhs) {
        return static_cast<bool>(rhs);
      }
      return *lhs < *rhs;
    }

    std::ostream &includePort(std::ostream &os) {
      os.iword(g_iosPortIndex) = 0;
      return os;
    }

    std::ostream &excludePort(std::ostream &os) {
      os.iword(g_iosPortIndex) = 1;
      return os;
    }
  }  // namespace io
}  // namespace span
