#include "span/exceptions/Demangle.hh"

#include <cxxabi.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace span {
  namespace exceptions {

    std::string demangle(const char* name) {
      int status;
      size_t len = 0;
      // malloc() memory for the demangled type name.
      char* demangled = abi::__cxa_demangle(name, nullptr, &len, &status);
      if (status != 0) {
        return std::string(name);
      }

      return std::string(demangled);
    }

    size_t demangle(const char* name, char* out, size_t outSize) {
      // TODO(kfc): Actually Demangle.
      return strlcpy(out, name, outSize);
    }

    size_t strlcpy(char* dest, const char* const src, size_t size) {
      size_t len = strlen(src);
      if (size != 0) {
        size_t n = std::min(len, size - 1);
        memcpy(dest, src, n);
        dest[n] = '\0';
      }
      return len;
    }

  }  // namespace exceptions
}  // namespace span
