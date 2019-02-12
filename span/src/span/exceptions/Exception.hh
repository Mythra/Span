#ifndef SPAN_SRC_SPAN_EXCEPTIONS_EXCEPTION_HH_
#define SPAN_SRC_SPAN_EXCEPTIONS_EXCEPTION_HH_

#include <errno.h>

#include "span/Common.hh"

#if UNIX_FLAVOUR == UNIX_FLAVOUR_BSD
#define error_t errno_t
#endif

#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
typedef int error_t;
#endif

error_t lastError();
void lastError(error_t error);

#endif  // SPAN_SRC_SPAN_EXCEPTIONS_EXCEPTION_HH_
