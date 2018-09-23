#ifndef SPAN_SRC_SPAN_IO_STREAMS_TLS_HH_
#define SPAN_SRC_SPAN_IO_STREAMS_TLS_HH_

#include <memory>
#include <string>
#include <vector>

#include "span/Common.hh"
#include "span/io/streams/Buffer.hh"
#include "span/io/streams/Filter.hh"

#include "absl/synchronization/mutex.h"
#include "openssl/ssl.h"

namespace span {
  namespace io {
    namespace streams {
      class BoringSSLException : public std::runtime_error {
      public:
        BoringSSLException();
        explicit BoringSSLException(const std::string message) : std::runtime_error(message) {}
      };

      class CertificateVerificationException : public BoringSSLException {
      public:
        explicit CertificateVerificationException(int32 verifyResult) :
          BoringSSLException(constructMessage(verifyResult)), verifyResult_(verifyResult) {}
        CertificateVerificationException(int32 verifyResult, const std::string msg) : BoringSSLException(msg),
          verifyResult_(verifyResult) {}

        int32 verifyResult() const { return verifyResult_; }

      private:
        static std::string constructMessage(int32 verifyResult);
        int32 verifyResult_;
      };

      class TLSStream : public MutatingFilterStream {
      public:
        typedef std::shared_ptr<TLSStream> ptr;

        static std::shared_ptr<SSL_CTX> generateSelfSignedCertificate(
          std::string commonName = std::string("localhost"));

        explicit TLSStream(Stream::ptr parent, bool client = true, bool own = true, SSL_CTX *ctx = NULL);

        bool supportsHalfClose() { return false; }

        void close(CloseType type = BOTH);
        using MutatingFilterStream::read;
        size_t read(void *buff, size_t len);
        size_t write(const Buffer *buff, size_t len);
        size_t write(const void *buff, size_t len);
        void flush(bool flushParent = true);

        void accept();
        void connect();

        void serverNameIndication(const std::string hostname);

        void verifyPeerCertificate();
        void verifyPeerCertificate(const std::string hostname);
        void clearTLSError();

      private:
        void wantRead();
        int sslCallWithLock(std::function<int()> dg, uint32 *error);

        absl::Mutex mutex_;
        std::shared_ptr<SSL_CTX> ctx_;
        std::shared_ptr<SSL> ssl_;
        Buffer readBuff_, writeBuff_;
        BIO *readBio, *writeBio;
      };
    }  // namespace streams
  }  // namespace io
}  // namespace span

#endif  // SPAN_SRC_SPAN_IO_STREAMS_TLS_HH_
