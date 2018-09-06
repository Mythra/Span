#ifndef SPAN_SRC_SPAN_EXCEPTIONS_DEMANGLE_HH_
#define SPAN_SRC_SPAN_EXCEPTIONS_DEMANGLE_HH_

#include <string>
#include <typeinfo>

#include "span/Common.hh"

namespace span {
  namespace exceptions {
    std::string demangle(const char* name);
    inline std::string demangle(const std::type_info& type) {
      return demangle(type.name());
    }

    size_t demangle(const char* name, char* buf, size_t bufSize);
    inline size_t demangle(const std::type_info& type, char* buf, size_t bufSize) {
      return demangle(type.name(), buf, bufSize);
    }

    size_t strlcpy(char* dest, const char* const src, size_t size);
  }  // namespace exceptions
}  // namespace span

#endif  // SPAN_SRC_SPAN_EXCEPTIONS_DEMANGLE_HH_
