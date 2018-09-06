#include "span/exceptions/Exception.hh"

error_t lastError() { return errno; }
void lastError(error_t error) { errno = error; }
