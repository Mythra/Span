#ifndef SPAN_SRC_SPAN_IO_SOCKET_HH_
#define SPAN_SRC_SPAN_IO_SOCKET_HH_

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "span/compat/Endian.hh"
#include "span/exceptions/Exception.hh"
#include "span/third_party/slimsig/slimsig.hh"

#if PLATFORM == PLATFORM_UNIX
#include <stdint.h>

#include <netinet/in.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#endif

#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#endif

namespace span {
  namespace io {
    class IOManager;

    typedef size_t iov_len_t;
    typedef int socket_t;

    struct Address;

    class Socket : public std::enable_shared_from_this<Socket> {
    public:
      typedef std::shared_ptr<Socket> ptr;
      typedef std::weak_ptr<Socket> weak_ptr;

      Socket(int family, int type, int protocol = 0);
      Socket(IOManager *ioManager, int family, int type, int protocol = 0);
      ~Socket();

      socket_t socket() { return sock_; }

      uint64 receiveTimeout() { return receiveTimeout_; }
      void receiveTimeout(uint64 us) { receiveTimeout_ = us; }
      uint64 sendTimeout() { return sendTimeout_; }
      void sendTimeout(uint64 us) { sendTimeout_ = us; }

      void bind(const Address &addr);
      void bind(const std::shared_ptr<Address> addr);

      void connect(const Address &to);
      void connect(const std::shared_ptr<Address> addr) { connect(*addr.get()); }

      void listen(int backlog = SOMAXCONN);

      Socket::ptr accept();

      void shutdown(int how = SHUT_RDWR);

      void getOption(int level, int option, void *result, size_t *len);
      template <class T>
      T getOption(int level, int option) {
          T result;
          size_t length = sizeof(T);
          getOption(level, option, &result, &length);
          return result;
      }

      void setOption(int level, int option, const void *value, size_t len);
      template <class T>
      void setOption(int level, int option, const T &value) {
          setOption(level, option, &value, sizeof(T));
      }

      void cancelAccept();
      void cancelConnect();
      void cancelSend();
      void cancelReceive();

      size_t send(const void *buffer, size_t length, int flags = 0);
      size_t send(const iovec *buffers, size_t length, int flags = 0);
      size_t sendTo(const void *buffer, size_t length, int flags, const Address &to);
      size_t sendTo(const void *buffer, size_t length, int flags, const std::shared_ptr<Address> to) {
        return sendTo(buffer, length, flags, *to.get());
      }
      size_t sendTo(const iovec *buffers, size_t length, int flags, const Address &to);
      size_t sendTo(const iovec *buffers, size_t length, int flags, const std::shared_ptr<Address> to) {
        return sendTo(buffers, length, flags, *to.get());
      }

      size_t receive(void *buffer, size_t length, int *flags = NULL);
      size_t receive(iovec *buffers, size_t length, int *flags = NULL);
      size_t receiveFrom(void *buffer, size_t length, Address *from, int *flags = NULL);
      size_t receiveFrom(iovec *buffers, size_t length, Address *from, int *flags = NULL);

      std::shared_ptr<Address> emptyAddress();
      std::shared_ptr<Address> remoteAddress();
      std::shared_ptr<Address> localAddress();

      int family() { return family_; }
      int type();
      int protocol() { return protocol_; }

      slimsig::signal_t<void()>::connection onRemoteClose(const std::function<void()>);

    private:
      socket_t sock_;
      uint64 receiveTimeout_, sendTimeout_;
      int family_, protocol_;
      IOManager *ioManager_;
      error_t cancelledSend_, cancelledReceive_;
      std::shared_ptr<Address> localAddress_, remoteAddress_;
      bool isConnected_, isRegisteredForRemoteClose_;
      slimsig::signal_t<void()> onRemoteClose_;

      Socket(IOManager *ioManager, int family, int type, int protocol, int initialize);

      template<bool isSend>
      size_t doIO(iovec *buffers, size_t len, int *flags, Address *address = NULL);
      static void callOnRemoteClose(Socket *self);
      void registerForRemoteClose();
      void accept(Socket::ptr target);
      void cancelIo(int event, error_t *cancelled, error_t error);
      Socket(const Socket&) = delete;
    };

    struct Address {
      public:
        typedef std::shared_ptr<Address> ptr;

        virtual ~Address() {}

        static std::vector<ptr> lookup(const std::string& host, int family = AF_UNSPEC, int type = 0, int protocol = 0);
        static std::multimap<std::string, std::pair<ptr, unsigned int>> getInterfaceAddresses(int family = AF_UNSPEC);
        static std::vector<std::pair<ptr, unsigned int>> getInterfaceAddresses(const std::string &iface,
          int family = AF_UNSPEC);
        static ptr create(const sockaddr *name, socklen_t nameLen);

        ptr clone();

        Socket::ptr createSocket(int type, int protocol = 0);
        Socket::ptr createSocket(IOManager *ioManager, int type, int protocol = 0);

        int family() const { return name()->sa_family; }
        virtual const sockaddr *name() const = 0;
        virtual sockaddr *name() = 0;
        virtual socklen_t nameLen() const = 0;
        virtual std::ostream & insert(std::ostream &os) const;
        virtual std::string to_string() const;

        bool operator<(const Address &rhs) const;
        bool operator==(const Address &rhs) const;
        bool operator!=(const Address &rhs) const;

      protected:
        Address() {}
    };

    struct IPAddress : public Address {
      public:
        typedef std::shared_ptr<IPAddress> ptr;

        static std::vector<ptr> lookup(const std::string& host, int family = AF_UNSPEC, int type = 0, int protocol = 0,
          int port = -1);
        static ptr create(const char *addr, uint16 port = 0);

        ptr clone();

        virtual ptr broadcastAddress(unsigned int prefixLen) = 0;
        virtual ptr networkAddress(unsigned int prefixLen) = 0;
        virtual ptr subnetMask(unsigned int prefixLen) = 0;

        virtual uint16 port() const = 0;
        virtual void port(uint16 p) = 0;
    };

    struct IPv4Address : public IPAddress {
      public:
        explicit IPv4Address(unsigned int addr = INADDR_ANY, uint16 port = 0);
        explicit IPv4Address(const char *addr, uint16 port = 0);

        ptr broadcastAddress(unsigned int prefixLen);
        ptr networkAddress(unsigned int prefixLen);
        ptr subnetMask(unsigned int prefixLen) { return IPv4Address::createSubnetMask(prefixLen); }
        static ptr createSubnetMask(unsigned int prefixLen);

        uint16 port() const { return byteswapOnLittleEndian(sin.sin_port); }
        void port(uint16 p) { sin.sin_port = byteswapOnLittleEndian(p); }

        const sockaddr *name() const { return reinterpret_cast<const sockaddr*>(&sin); }
        sockaddr *name() { return reinterpret_cast<sockaddr*>(&sin); }
        socklen_t nameLen() const { return sizeof(sockaddr_in); }

        std::ostream & insert(std::ostream &os) const;
        std::string to_string() const;
      private:
        sockaddr_in sin;
    };

    struct IPv6Address : public IPAddress {
      public:
        IPv6Address();
        explicit IPv6Address(const unsigned char address[16], uint16 port = 0);
        explicit IPv6Address(const char *addr, uint16 port = 0);

        ptr broadcastAddress(unsigned int prefixLen);
        ptr networkAddress(unsigned int prefixLen);
        ptr subnetMask(unsigned int prefixLen) { return createSubnetMask(prefixLen); }
        static ptr createSubnetMask(unsigned int prefixLen);

        uint16 port() const { return byteswapOnLittleEndian(sin.sin6_port); }
        void port(uint16 p) { sin.sin6_port = byteswapOnLittleEndian(p); }

        const sockaddr *name() const { return reinterpret_cast<const sockaddr*>(&sin); }
        sockaddr *name() { return reinterpret_cast<sockaddr *>(&sin); }
        socklen_t nameLen() const { return sizeof(sockaddr_in6); }

        std::ostream & insert(std::ostream &os) const;
        std::string to_string() const;
      private:
        sockaddr_in6 sin;
    };

#if PLATFORM != PLATFORM_WIN32
    struct UnixAddress : public Address {
      public:
        UnixAddress();
        explicit UnixAddress(const std::string &path);

        const sockaddr *name() const { return reinterpret_cast<const sockaddr*>(&sun); }
        sockaddr *name() { return reinterpret_cast<sockaddr*>(&sun); }
        socklen_t nameLen() const { return len; }
        void nameLen(size_t length) { len = length; }

        std::ostream & insert(std::ostream &os) const;
        std::string to_string() const;
        static const size_t MAX_PATH_LEN;
      private:
        size_t len;
        struct sockaddr_un sun;
    };
#endif

    struct UnknownAddress : public Address {
      public:
        explicit UnknownAddress(int family);

        const sockaddr *name() const { return &sa; }
        sockaddr *name() { return &sa; }
        socklen_t nameLen() const { return sizeof(sockaddr); }

      private:
        sockaddr sa;
    };

    std::ostream &operator <<(std::ostream &os, const Address &addr);

    bool operator<(const Address::ptr &lhs, const Address::ptr &rhs);

    std::ostream &includePort(std::ostream &os);
    std::ostream &excludePort(std::ostream &os);
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_SOCKET_HH_
