#ifndef SPAN_SRC_SPAN_EXCEPTIONS_ASSERT_HH_
#define SPAN_SRC_SPAN_EXCEPTIONS_ASSERT_HH_

#include <exception>

#include "glog/logging.h"

namespace span {
  namespace exceptions {
#define SPAN_ASSERT(x)                        \
  if (!(x)) {                                 \
    LOG(FATAL) << "Assertion Failed: " #x;    \
    ::std::terminate();                       \
  }

#define SPAN_NOT_REACHED(x)                      \
  LOG(FATAL) << "Reached unreachable area: " #x; \
  ::std::terminate();

#define SPAN_VERIFY(x) SPAN_ASSERT(x)

  }  // namespace exceptions
}  // namespace span

#endif  // SPAN_SRC_SPAN_EXCEPTIONS_ASSERT_HH_
