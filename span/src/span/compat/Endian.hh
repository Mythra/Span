#ifndef SPAN_SRC_SPAN_COMPAT_ENDIAN_HH_
#define SPAN_SRC_SPAN_COMPAT_ENDIAN_HH_

#include <type_traits>

#include "span/Common.hh"

#define SPAN_LITTLE_ENDIAN 1
#define SPAN_BIG_ENDIAN 2

#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
#include <libkern/OSByteOrder.h>
#include <stdint.h>
#include <sys/_endian.h>
#elif UNIX_FLAVOUR == UNIX_FLAVOUR_BSD
#include <stdint.h>
#include <sys/endian.h>
#else
#include <byteswap.h>
#include <stdint.h>
#endif

namespace span {
#if HAVE_DARWIN == 1 || PLATFORM == PLATFORM_APPLE
  template<class T>
  typename std::enable_if<sizeof(T) == sizeof(uint64), T>::type
  byteswap(T value) {
    return static_cast<T>(_OSSwapInt64(static_cast<uint64>(value)));
  }

  template<class T>
  typename std::enable_if<sizeof(T) == sizeof(uint32), T>::type
  byteswap(T value) {
    return static_cast<T>(_OSSWapInt32(static_cast<uint32>(value)));
  }

  template<class T>
  typename std::enable_if<sizeof(T) == sizeof(uint16), T>::type
  byteswap(T value) {
    return static_cast<T>(_OSSwapInt16(static_cast<uint16>(value)));
  }

#if !defined(__BIG_ENDIAN__) && !defined(__LITTLE_ENDIAN__)
#error Do not know the endianess of this arch
#endif

#ifdef __BIG_ENDIAN__
#define SPAN_BYTE_ORDER SPAN_BIG_ENDIAN
#else
#define SPAN_BYTE_ORDER SPAN_LITTLE_ENDIAN
#endif

#elif UNIX_FLAVOUR == UNIX_FLAVOUR_BSD
  template<class T>
  typename std::enable_if<sizeof(T) == sizeof(uint64), T>::type
  byteswap(T value) {
    return static_cast<T>(bswap64(static_cast<uint64>(value)));
  }

  template<class T>
  typename std::enable_if<sizeof(T) == sizeof(uint32), T>::type
  byteswap(T value) {
    return static_cast<T>(bswap32(static_cast<uint32>(value)));
  }

  template<class T>
  typename std::enable_if<sizeof(T) == sizeof(uint16), T>::type
  byteswap(T value) {
    return static_cast<T>(bswap32(static_cast<uint16>(value)));
  }

#if _BYTE_ORDER == _BIG_ENDIAN
#define SPAN_BYTE_ORDER SPAN_BIG_ENDIAN
#else
#define SPAN_BYTE_ORDER SPAN_LITTLE_ENDIAN
#endif

#else

  template<class T>
  typename std::enable_if<sizeof(T) == sizeof(uint64), T>::type
  byteswap(T value) {
    return static_cast<T>(bswap_64(static_cast<uint64>(value)));
  }

  template<class T>
  typename std::enable_if<sizeof(T) == sizeof(uint32), T>::type
  byteswap(T value) {
    return static_cast<T>(bswap_32(static_cast<uint32>(value)));
  }

  template<class T>
  typename std::enable_if<sizeof(T) == sizeof(uint16), T>::type
  byteswap(T value) {
    return static_cast<T>(bswap_16(static_cast<uint16>(value)));
  }

#if BYTE_ORDER == BIG_ENDIAN
#define SPAN_BYTE_ORDER SPAN_BIG_ENDIAN
#else
#define SPAN_BYTE_ORDER SPAN_LITTLE_ENDIAN
#endif

#endif

#if SPAN_BYTE_ORDER == SPAN_BIG_ENDIAN
  template<class T>
  T byteswapOnLittleEndian(T t) {
    return t;
  }

  template<class T>
  T byteswapOnBigEndian(T t) {
    return byteswap(t);
  }
#else
  template<class T>
  T byteswapOnLittleEndian(T t) {
    return byteswap(t);
  }

  template<class T>
  T byteswapOnBigEndian(T t) {
    return t;
  }
#endif
}  // namespace span

#endif  // SPAN_SRC_SPAN_COMPAT_ENDIAN_HH_
