#ifndef SPAN_SRC_SPAN_COMMON_HH_
#define SPAN_SRC_SPAN_COMMON_HH_

#if defined( __WIN32__ ) || defined( WIN32 ) || defined( _WIN32 )
#define WIN32_LEAN_AND_MEAN  // Cut out Deps
#include <windows.h>
#undef NOMINMAX
#endif

#define PLATFORM_WIN32  0
#define PLATFORM_UNIX   1
#define PLATFORM_DARWIN 2

#define UNIX_FLAVOUR_LINUX 1
#define UNIX_FLAVOUR_BSD   2
#define UNIX_FLAVOUR_OTHER 3
#define UNIX_FLAVOUR_OSX   4

#if defined(__WIN32__) || defined(WIN32) || defined(_WIN32)
#  define PLATFORM PLATFORM_WIN32
#elif defined(__APPLE_CC__)
#  define PLATFORM PLATFORM_DARWIN
#include <unistd.h>
#define POSIX  //
#else
#  define PLATFORM PLATFORM_UNIX
#include <unistd.h>
#define POSIX  //
#endif

#if PLATFORM == PLATFORM_WIN32
#include "./compat/pstdint.h"
#else
#include <stdint.h>
#endif

typedef int64_t int64;
typedef int32_t int32;
typedef int16_t int16;
typedef int8_t int8;
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;

#if (defined(__APPLE__) && defined(__MACH__))
#define HAVE_DARWIN 1  //
#endif

#if PLATFORM == PLATFORM_UNIX || PLATFORM == PLATFORM_APPLE
#ifdef HAVE_DARWIN
#define PLATFORM_TEXT "MacOSX"
#define UNIX_FLAVOUR UNIX_FLAVOUR_OSX
#else
#if (defined(__FreeBSD__))
#define PLATFORM_TEXT "FreeBSD"
#define UNIX_FLAVOUR UNIX_FLAVOUR_BSD
#else
#define PLATFORM_TEXT "Linux"
#define UNIX_FLAVOUR UNIX_FLAVOUR_LINUX
#endif
#endif
#endif
#if PLATFORM == PLATFORM_WIN32
#define PLATFORM_TEXT "Win32"
#endif

#ifdef WIN32
#define FORCEINLINE __forceinline
#else
#define FORCEINLINE inline
#endif
#define INLINE inline

#if PLATFORM == PLATFORM_WIN32
#define EXPORT __declspec(dllexport)
#define IMPORT __declspec(dllimport)
#else
#define EXPORT __attribute__((visibility("default")))
#define IMPORT
#endif

#ifdef X64
#define ARCH "X64"
#else
#define ARCH "X86"
#endif

#if PLATFORM == PLATFORM_WIN32
#define STRCASECMP stricmp
#else
#define STRCASECMP strcasecmp
#endif

#if PLATFORM == PLATFORM_DARWIN || UNIX_FLAVOUR == UNIX_FLAVOUR_OSX
typedef unsigned short u_short;  // NOLINT(runtime/int)
typedef unsigned int u_int;      // NOLINT(runtime/int)
typedef unsigned long u_long;    // NOLINT(runtime/int)
typedef unsigned char u_char;

#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 201410L
#define _XOPEN_SOURCE 600
#endif

namespace span {
#ifdef _MSC_VER
#pragma float_control(push)
#pragma float_control(precise, on)
#endif

  FORCEINLINE int int32abs(const int value) {
    return (value ^ (value >> 31)) - (value >> 31);
  }

  FORCEINLINE int float2int32(const float value) {
#if !defined(X64) && _MSC_VER && !defined(USING_BIG_ENDIAN)
    int i;
    __asm {
      fld value
      frndint
      fistp i
    }
    return i;
#else
    union { int asInt[2]; double asDouble; } uni;
    uni.asDouble = value + 6755399441055744.0;

    return uni.asInt[0];
#endif
  }

  FORCEINLINE int double2int32(const double value) {
#if !defined(X64) && _MSC_VER && !defined(USING_BIG_ENDIAN)
    int i;
    __asm {
      fld value
      frndint
      fistp i
    }
    return i;
#else
    union { int asInt[2]; double asDouble; } uni;
    uni.asDouble = value + 6755399441055744.0;

    return uni.asInt[0];
#endif
  }

  namespace private_noncopyable {  // protection from unintended ADL.
    class noncopyable {
    protected:
      noncopyable() {}
      ~noncopyable() {}
    private:
      noncopyable(const noncopyable&);
      const noncopyable& operator= (const noncopyable&);
    };
  }  // namesapce private_noncopyable

  typedef private_noncopyable::noncopyable noncopyable;

  template <class T>
  void nop(const T &) {}
}  // namespace span

#endif  // SPAN_SRC_SPAN_COMMON_HH_
