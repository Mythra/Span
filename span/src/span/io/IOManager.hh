#ifndef SPAN_SRC_SPAN_IO_IOMANAGER_HH_
#define SPAN_SRC_SPAN_IO_IOMANAGER_HH_

#include "span/Common.hh"

#if PLATFORM == PLATFORM_WIN32
#error "Yell at someone to add windows support for IOManager."
#elif PLATFORM == PLATFORM_UNIX
#if UNIX_FLAVOUR != UNIX_FLAVOUR_BSD && UNIX_FLAVOUR != UNIX_FLAVOUR_OSX
#include "span/io/IOManagerEpoll.hh"
#else
#include "span/io/IOManagerKqueue.hh"
#endif
#elif PLATFORM == PLATFORM_DARWIN
#include "span/io/IOManagerKqueue.hh"
#endif

#endif  // SPAN_SRC_SPAN_IO_IOMANAGER_HH_
