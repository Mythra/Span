#ifndef SPAN_SRC_SPAN_EXCEPTIONS_EXCEPTION_HH_
#define SPAN_SRC_SPAN_EXCEPTIONS_EXCEPTION_HH_

#include <errno.h>

error_t lastError();
void lastError(error_t error);

#endif  // SPAN_SRC_SPAN_EXCEPTIONS_EXCEPTION_HH_
