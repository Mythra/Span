#include "span/io/streams/Tls.hh"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>

#include "span/Common.hh"
#include "span/exceptions/Assert.hh"

#include "glog/logging.h"
#include "openssl/err.h"
#include "openssl/evp.h"
#include "openssl/x509v3.h"

namespace span {
  namespace io {
    namespace streams {
      namespace {
        static struct TLSInitializer {
          TLSInitializer() {
            SSL_library_init();
            SSL_load_error_strings();
          }
          ~TLSInitializer() {
            ERR_free_strings();
            CRYPTO_cleanup_all_ex_data();
            EVP_cleanup();
          }
        } gInit;
      }  // namespace

      static bool hasBoringSSLError() {
        uint32 err = ERR_peek_error();
        return (err != SSL_ERROR_NONE);
      }

      static std::string getBoringSSLErrorMessage() {
        std::ostringstream os;
        uint32 err;
        char buff[120];
        while ((err = ERR_get_error()) != SSL_ERROR_NONE) {
          if (!os.str().empty()) {
            os << "\n";
          }
          os << ERR_error_string(err, buff);
        }
        return os.str();
      }

      BoringSSLException::BoringSSLException() : std::runtime_error(getBoringSSLErrorMessage()) {}

      std::string CertificateVerificationException::constructMessage(int32 verifyResult) {
        return X509_verify_cert_error_string(verifyResult);
      }

      std::shared_ptr<SSL_CTX> TLSStream::generateSelfSignedCertificate(const std::string commonName) {
        std::shared_ptr<SSL_CTX> ctx;
        ctx.reset(SSL_CTX_new(TLS_method()), &SSL_CTX_free);
        std::shared_ptr<X509> cert;
        std::shared_ptr<EVP_PKEY> pkey;

        pkey.reset(EVP_PKEY_new(), &EVP_PKEY_free);
        if (!pkey) {
          throw std::runtime_error("bad alloc");
        }

        // Use secp384r1, since chrome does not support secp521r1.
        int eccgrp = OBJ_txt2nid("secp384r1");
        if (eccgrp == NID_undef) {
          throw std::runtime_error("curve secp384r1 not supported");
        }
        EC_KEY *ec_key = EC_KEY_new_by_curve_name(eccgrp);
        if (!ec_key) {
          throw std::runtime_error("EC_KEY_new_by_curve_name error");
        }
        EC_KEY_set_asn1_flag(ec_key, OPENSSL_EC_NAMED_CURVE);
        if (!EC_KEY_generate_key(ec_key)) {
          throw std::runtime_error("EC_KEY_generate_key error");
        }
        SPAN_ASSERT(EVP_PKEY_assign_EC_KEY(pkey.get(), ec_key) == 1);

        cert.reset(X509_new(), &X509_free);
        if (!cert) {
          throw std::runtime_error("bad alloc");
        }

        X509_set_version(cert.get(), 2);
        // Use 1 instead of 0, since some HTTP Servers/Clients are dumb.
        ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
        // We're valid for a year.
        X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
        X509_gmtime_adj(X509_get_notAfter(cert.get()), 31536000L);
        X509_set_pubkey(cert.get(), pkey.get());
        // Now we set our full name.
        X509_NAME * name = X509_get_subject_name(cert.get());
        X509_NAME_add_entry_by_txt(
          name,
          "C",
          MBSTRING_ASC,
          reinterpret_cast<const unsigned char *>("US"),
          -1,
          -1,
          0);
        X509_NAME_add_entry_by_txt(
          name,
          "O",
          MBSTRING_ASC,
          reinterpret_cast<const unsigned char*>("SelfSigned, Inc."),
          -1,
          -1,
          0);
        X509_NAME_add_entry_by_txt(
          name,
          "CN",
          MBSTRING_ASC,
          // Ensure it's null terminated.
          reinterpret_cast<const unsigned char *>(commonName.c_str()),
          -1,
          -1,
          0);
        X509_set_issuer_name(cert.get(), name);
        // Sign the certificate.
        X509_sign(cert.get(), pkey.get(), EVP_sha512());

        // Have the ctx use the certificate/key.
        SSL_CTX_use_certificate(ctx.get(), cert.get());
        SSL_CTX_use_PrivateKey(ctx.get(), pkey.get());

        return ctx;
      }

      TLSStream::TLSStream(Stream::ptr parent, bool client, bool own, SSL_CTX *ctx) :
        MutatingFilterStream(parent, own) {
        SPAN_ASSERT(parent);
        clearTLSError();
        if (ctx) {
          ctx_.reset(ctx, &nop<SSL_CTX *>);
        } else if (!client) {
          // Generate our own cert.
          ctx_ = generateSelfSignedCertificate();
        } else {
          ctx_.reset(SSL_CTX_new(TLS_method()), &SSL_CTX_free);
        }

        if (!ctx_) {
          SPAN_ASSERT(hasBoringSSLError());
          throw BoringSSLException(getBoringSSLErrorMessage());
        }

        ssl_.reset(SSL_new(ctx_.get()), &SSL_free);

        if (!ssl_) {
          SPAN_ASSERT(hasBoringSSLError());
          throw BoringSSLException(getBoringSSLErrorMessage());
        }

        readBio = BIO_new(BIO_s_mem());
        writeBio = BIO_new(BIO_s_mem());
        if (!readBio || !writeBio) {
          if (readBio) {
            BIO_free(readBio);
          }
          if (writeBio) {
            BIO_free(writeBio);
          }
          SPAN_ASSERT(hasBoringSSLError());
          throw BoringSSLException(getBoringSSLErrorMessage());
        }

        BIO_set_mem_eof_return(readBio, -1);

        SSL_set_bio(ssl_.get(), readBio, writeBio);
      }

      void TLSStream::clearTLSError() {
        ERR_clear_error();
      }

      void TLSStream::close(CloseType type) {
        SPAN_ASSERT(type == BOTH);
        if (!(sslCallWithLock(std::bind(SSL_get_shutdown, ssl_.get()), NULL) & SSL_SENT_SHUTDOWN)) {
          uint32 error = SSL_ERROR_NONE;
          const int32 result = sslCallWithLock(std::bind(SSL_shutdown, ssl_.get()), &error);
          if (result <= 0) {
            DLOG(INFO) << this << " SSL_shutdown(" << ssl_.get() << "): " << result << " (" << error << ")";
            if (error != SSL_ERROR_NONE && error != SSL_ERROR_ZERO_RETURN) {
              throw BoringSSLException(getBoringSSLErrorMessage());
            }
          }

          flush(false);
        }

        while (!(sslCallWithLock(std::bind(SSL_get_shutdown, ssl_.get()), NULL) & SSL_RECEIVED_SHUTDOWN)) {
          uint32 error = SSL_ERROR_NONE;
          const int32 result = sslCallWithLock(std::bind(SSL_shutdown, ssl_.get()), &error);
          DLOG(INFO) << this << " SSL_shutdown(" << ssl_.get() << "): " << result << " (" << error << ")";
          if (result > 0) {
            break;
          }

          switch (error) {
            case SSL_ERROR_NONE:
            case SSL_ERROR_ZERO_RETURN:
              break;
            case SSL_ERROR_WANT_READ:
              flush();
              wantRead();
              continue;
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
              SPAN_NOT_REACHED("SSLError");
            case SSL_ERROR_SYSCALL:
              if (hasBoringSSLError()) {
                std::string message = getBoringSSLErrorMessage();
                LOG(ERROR) << this << " SSL_shutdown(" << ssl_.get() << "): "
                  << result << " (" << error << ", " << message << ")";
                throw BoringSSLException(message);
              }
              LOG(WARNING) << this << " SSL_shutdown(" << ssl_.get() << "): "
                << result << " (" << error << ")";
              if (result == 0) {
                break;
              }
            case SSL_ERROR_SSL:
              {
                SPAN_ASSERT(hasBoringSSLError());
                std::string message = getBoringSSLErrorMessage();
                LOG(ERROR) << this << " SSL_shutdown(" << ssl_.get() << "): "
                  << result << " (" << error << ", " << message << ")";
                throw BoringSSLException(message);
              }
            default:
              SPAN_NOT_REACHED("default openssl while");
          }
          break;
        }

        parent()->close();
      }

      size_t TLSStream::read(void *buff, size_t len) {
        const size_t toRead = std::min<size_t>(0x0fffffff, len);
        while (true) {
          uint32 error = SSL_ERROR_NONE;
          const int32 result = sslCallWithLock(std::bind(SSL_read, ssl_.get(), buff, toRead), &error);
          if (result > 0) {
            return result;
          }

          DLOG(INFO) << this << " SSL_read(" << ssl_.get() << ", " << toRead
            << "): " << result << " (" << error << ")";
          switch (error) {
            case SSL_ERROR_NONE:
              return result;
            case SSL_ERROR_ZERO_RETURN:
              // Received close_notify message.
              SPAN_ASSERT(result == 0);
              return 0;
            case SSL_ERROR_WANT_READ:
              wantRead();
              continue;
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
              SPAN_NOT_REACHED("ssl error multi-case switch.");
            case SSL_ERROR_SYSCALL:
              if (hasBoringSSLError()) {
                std::string message = getBoringSSLErrorMessage();
                LOG(ERROR) << this << " SSL_read(" << ssl_.get() << ", "
                  << toRead << "): " << result << " (" << error << ", " << message << ")";
                throw BoringSSLException(message);
              }
              LOG(WARNING) << this << " SSL_read(" << ssl_.get() << ", "
                << toRead << "): " << result << " (" << error << ")";
              if (result == 0) {
                return 0;
              }
              throw std::runtime_error("SSL_read");
            case SSL_ERROR_SSL:
              {
                SPAN_ASSERT(hasBoringSSLError());
                std::string message = getBoringSSLErrorMessage();
                LOG(ERROR) << this << " SSL_read(" << ssl_.get() << ", "
                  << toRead << "): " << result << " (" << error << ", " << message << ")";
                throw BoringSSLException(message);
              }
            default:
              SPAN_NOT_REACHED("default openssl switch");
          }
        }
      }

      size_t TLSStream::write(const Buffer *buff, size_t len) {
        // SSL_write will create at least two SSL records for each call -
        // one for data, and one tiny one for the checksum or IV or something.
        // Dealing with lots of extra records can take some serious CPU time
        // server-side, so we want to provide it with as much data as possible,
        // even if that means reallocating.  That's why we use pass the flag to
        // coalesce small segments, instead of only doing the first available
        // segment.
        return Stream::write(buff, len, true);
      }

      size_t TLSStream::write(const void *buff, size_t len) {
        flush(false);

        if (len == 0) {
          return 0;
        }

        const size_t toWrite = std::min<size_t>(0x7fffffff, len);
        while (true) {
          uint32 error = SSL_ERROR_NONE;
          const int32 result = sslCallWithLock(std::bind(SSL_write, ssl_.get(), buff, toWrite), &error);
          if (result > 0) {
            return result;
          }

          DLOG(INFO) << this << " SSL_write(" << ssl_.get() << ", " << toWrite
            << "): " << result << " (" << error << ")";
          switch (error) {
            case SSL_ERROR_NONE:
              return result;
            case SSL_ERROR_ZERO_RETURN:
              // Received close_notify message.
              SPAN_ASSERT(result != 0);
              return result;
            case SSL_ERROR_WANT_READ:
              BoringSSLException("SSL_write generated SSL_ERROR_WANT_READ");
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
              SPAN_NOT_REACHED("SSL_WRITE multi-case switch");
            case SSL_ERROR_SYSCALL:
              if (hasBoringSSLError()) {
                std::string message = getBoringSSLErrorMessage();
                LOG(ERROR) << this << " SSL_write(" << ssl_.get() << ", "
                  << toWrite << "): " << result << " (" << error << ", " << message << ")";
                throw BoringSSLException(message);
              }
              LOG(ERROR) << this << " SSL_write(" << ssl_.get() << ", "
                << toWrite << "): " << result << " (" << error << ")";
              throw std::runtime_error("SSL_write");
            case SSL_ERROR_SSL:
              {
                SPAN_ASSERT(hasBoringSSLError());
                std::string msg = getBoringSSLErrorMessage();
                LOG(ERROR) << this << " SSL_write(" << ssl_.get() << ", "
                  << toWrite << "): " << result << " (" << error << ", " << msg << ")";
                throw BoringSSLException(msg);
              }
            default:
              SPAN_NOT_REACHED("SSL_WRITE switch default");
          }
        }
      }

      void TLSStream::flush(bool flushParent) {
        static const int32 WRITE_BUF_LENGTH = 4096;
        char writeBuff[WRITE_BUF_LENGTH];
        int32 toWrite = 0;
        do {
          toWrite = BIO_read(writeBio, static_cast<void*>(writeBuff), WRITE_BUF_LENGTH);
          if (toWrite > 0) {
            writeBuff_.copyIn(static_cast<const void*>(writeBuff), toWrite);
          }
        } while (toWrite > 0);

        while (writeBuff_.readAvailable()) {
          DLOG(INFO) << this << " parent()->write(" << writeBuff_.readAvailable() << ")";
          size_t written = parent()->write(&writeBuff_, writeBuff_.readAvailable());
          DLOG(INFO) << this << " parent()->write(" << writeBuff_.readAvailable() << "): " << written;
          writeBuff_.consume(written);
        }

        if (flushParent) {
          parent()->flush(flushParent);
        }
      }

      void TLSStream::accept() {
        while (true) {
          uint32 error = SSL_ERROR_NONE;
          const int32 result = sslCallWithLock(std::bind(SSL_accept, ssl_.get()), &error);
          if (result > 0) {
            flush(false);
            return;
          }
          DLOG(INFO) << this << " SSL_accept(" << ssl_.get() << "): "
            << result << " (" << error << ")";
          switch (error) {
            case SSL_ERROR_NONE:
              flush(false);
              return;
            case SSL_ERROR_ZERO_RETURN:
              // Received close_notify message.
              return;
            case SSL_ERROR_WANT_READ:
              flush();
              wantRead();
              continue;
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
              SPAN_NOT_REACHED("TlsStream::accept multi case");
            case SSL_ERROR_SYSCALL:
              if (hasBoringSSLError()) {
                std::string message = getBoringSSLErrorMessage();
                LOG(ERROR) << this << " SSL_accept(" << ssl_.get() << "): "
                  << result << " (" << error << ", " << message << ")";
                throw BoringSSLException(message);
              }
              LOG(ERROR) << this << " SSL_accept(" << ssl_.get() << "): "
                << result << " (" << error << ")";
              if (result == 0) {
                throw std::runtime_error("Unexpected EoF");
              }
              throw std::runtime_error("SSL_accept");
            case SSL_ERROR_SSL:
              {
                SPAN_ASSERT(hasBoringSSLError());
                std::string message = getBoringSSLErrorMessage();
                LOG(ERROR) << this << " SSL_accept(" << ssl_.get() << "): "
                  << result << " (" << error << ", " << message << ")";
                throw BoringSSLException(message);
              }
            default:
              SPAN_NOT_REACHED("TlsStream::accept() default switch case");
          }
        }
      }

      void TLSStream::connect() {
        while (true) {
          uint32 error = SSL_ERROR_NONE;
          const int32 result = sslCallWithLock(std::bind(SSL_connect, ssl_.get()), &error);
          DLOG(INFO) << this << " SSL_connect(" << ssl_.get() << "): " << result
            << " (" << error << ")";
          if (result > 0) {
            flush(false);
            return;
          }

          switch (error) {
            case SSL_ERROR_NONE:
              flush(false);
              return;
            case SSL_ERROR_ZERO_RETURN:
              // Received close_notify message.
              return;
            case SSL_ERROR_WANT_READ:
              flush();
              wantRead();
              continue;
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_X509_LOOKUP:
              SPAN_NOT_REACHED("TlsStream::connect multi-case switch");
            case SSL_ERROR_SYSCALL:
              if (hasBoringSSLError()) {
                std::string msg = getBoringSSLErrorMessage();
                LOG(ERROR) << this << " SSL_connect(" << ssl_.get() << "): "
                  << result << " (" << error << ", " << msg << ")";
                throw BoringSSLException(msg);
              }
              LOG(ERROR) << this << " SSL_connect(" << ssl_.get() << "): "
                << result << " (" << error << ")";
              if (result == 0) {
                throw std::runtime_error("Unexpected EoF exception");
              }
              throw std::runtime_error("SSL_connect");
            case SSL_ERROR_SSL:
              {
                SPAN_ASSERT(hasBoringSSLError());
                std::string msg = getBoringSSLErrorMessage();
                LOG(ERROR) << this << " SSL_connect(" << ssl_.get() << "): "
                  << result << " (" << error << ", " << msg << ")";
                throw BoringSSLException(msg);
              }
            default:
              SPAN_NOT_REACHED("TLSStream::connect default switch statement.");
          }
        }
      }

      void TLSStream::serverNameIndication(std::string hostname) {
        absl::MutexLock _lock(&mutex_);
        // Ensure we have null terminator.
        if (!SSL_set_tlsext_host_name(ssl_.get(), hostname.c_str())) {
          if (!hasBoringSSLError()) {
            return;
          }
          std::string msg = getBoringSSLErrorMessage();
          LOG(ERROR) << this << " SSL_set_tlsext_host_name(" << ssl_.get()
            << ", " << hostname << "): " << msg;
          throw BoringSSLException(msg);
        }
      }

      void TLSStream::verifyPeerCertificate() {
        const int32 verifyResult = sslCallWithLock(std::bind(SSL_get_verify_result, ssl_.get()), NULL);
        if (verifyResult) {
          LOG(WARNING) << this << " SSL_get_verify_result(" << ssl_.get() << "): " << verifyResult;
        } else {
          DLOG(INFO) << this << " SSL_get_verify_result(" << ssl_.get() << "): " << verifyResult;
        }
        if (verifyResult != X509_V_OK) {
          throw CertificateVerificationException(verifyResult);
        }
      }

      void TLSStream::verifyPeerCertificate(const std::string hostname) {
        if (hostname.empty()) {
          throw CertificateVerificationException(
            X509_V_ERR_APPLICATION_VERIFICATION,
            "No hostname given");
        }

        absl::MutexLock _lock(&mutex_);
        std::shared_ptr<X509> crt;
        crt.reset(SSL_get_peer_certificate(ssl_.get()), &X509_free);
        if (!crt) {
          throw CertificateVerificationException(
            X509_V_ERR_APPLICATION_VERIFICATION,
            "No certificate presented");
        }

        // Ensure null terminated.
        int32 err = X509_check_host(crt.get(), hostname.c_str(), hostname.length(), 0, NULL);
        if (err < 0) {
          throw CertificateVerificationException(err);
        }
      }

      void TLSStream::wantRead() {
        if (readBuff_.readAvailable() == 0) {
          DLOG(INFO) << this <<  " parent()->read(32768)";
          const size_t result = parent()->read(&readBuff_, 32768);
          DLOG(INFO) << this << " parent()->read(32768): " << result;
          if (result == 0) {
            BIO_set_mem_eof_return(readBio, 0);
            return;
          }
        }

        SPAN_ASSERT(readBuff_.readAvailable());
        const iovec iov = readBuff_.readBuffer(~0, false);
        SPAN_ASSERT(iov.iov_len > 0);
        const int32 written = BIO_write(readBio, static_cast<char *>(iov.iov_base), iov.iov_len);
        SPAN_ASSERT(written > 0);
        readBuff_.consume(written);
        DLOG(INFO) << this << " wantRead(): " << written;
      }

      int TLSStream::sslCallWithLock(std::function<int()> dg, uint32 *error) {
        absl::MutexLock _lock(&mutex_);

        // If error is NULL, it means that sslCallWithLock is not supposed to call
        // SSL_get_error after dg got called. If SSL_get_error is not supposed to be
        // called, there is no need to clear current threads error queue.
        if (error == NULL) {
          LOG(ERROR) << "sslCallWithLock called without check SSL_get_error!";
          return dg();
        }

        clearTLSError();
        const int result = dg();
        if (result <= 0) {
          *error = SSL_get_error(ssl_.get(), result);
        }
        return result;
      }
    }  // namespace streams
  }  // namespace io
}  // namespace span
