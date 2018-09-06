#ifndef SPAN_SRC_SPAN_EXCEPTIONS_EXCEPTIONSTRING_HH_
#define SPAN_SRC_SPAN_EXCEPTIONS_EXCEPTIONSTRING_HH_

#include <exception>
#include <string>
#include <typeinfo>
#include <type_traits>

#include "span/Common.hh"
#include "span/exceptions/Demangle.hh"

namespace span {
  namespace exceptions {

    inline std::string ExceptionStr(const std::exception& e) {
      std::string rv(demangle(typeid(e)));
      rv += ": ";
      rv += e.what();
      return rv;
    }

    // Check if exception_ptr is enabled
#if defined(__GNUC__) && defined(__GCC_ATOMIC_INT_LOCK_FREE) && __GCC_ATOMIC_INT_LOCK_FREE > 1
    inline std::string ExceptionStr(std::exception_ptr ep) {
      try {
        std::rethrow_exception(ep);
      } catch (std::exception& e) {
        return ExceptionStr(e);
      } catch (...) {
        return std::string("<unknown exception>");
      }
    }
#endif

  }  // namespace exceptions
}  // namespace span

#endif  // SPAN_SRC_SPAN_EXCEPTIONS_EXCEPTIONSTRING_HH_
