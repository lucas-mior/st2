// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(PRIMITIVES_H)
#define PRIMITIVES_H

#if defined(CHAR_BIT)
_Static_assert(CHAR_BIT == 8, "primitives.h requires CHAR_BIT == 8");
#endif

#define CHAR_BIT2 8

_Static_assert(~(0ul) == 18446744073709551615ul,
               "primitives.h requires unsigned long to be 64 bits");
_Static_assert((unsigned char)~0 == (unsigned char)255,
               "primitives.h requires CHAR_BIT == 8");
_Static_assert(sizeof(char)*CHAR_BIT2      == 8,  "char must be 8 bits");
_Static_assert(sizeof(short)*CHAR_BIT2     == 16, "short must be 16 bits");
_Static_assert(sizeof(int)*CHAR_BIT2       == 32, "int must be 32 bits");
_Static_assert(sizeof(long long)*CHAR_BIT2 == 64, "long long must be 64 bits");
_Static_assert(sizeof(void *)*CHAR_BIT2    == 64, "pointers must be 64 bits");

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned long long ullong;

typedef signed char schar;
typedef long long llong;
#if !defined(__CPROC__)
typedef long double ldouble;
#else
typedef double ldouble;
#endif

typedef schar  int8;
typedef short  int16;
typedef int    int32;
typedef llong  int64;
typedef uchar  uint8;
typedef ushort uint16;
typedef uint   uint32;
typedef ullong uint64;

typedef ullong uintptr;
typedef llong  intptr;

_Static_assert(sizeof(uintptr) == sizeof(void *),
               "uintptr must match pointer width");
_Static_assert(sizeof(intptr) == sizeof(void *),
               "intptr must match pointer width");

#if !defined(SCHAR_MIN)
#define SCHAR_MIN (-127 - 1)
#endif
#if !defined(SCHAR_MAX)
#define SCHAR_MAX 127
#endif
#if !defined(UCHAR_MAX)
#define UCHAR_MAX 255
#endif
#if !defined(CHAR_MIN)
#define CHAR_MIN (((char)-1 < 0) ? SCHAR_MIN : 0)
#endif
#if !defined(CHAR_MAX)
#define CHAR_MAX (((char)-1 < 0) ? SCHAR_MAX : UCHAR_MAX)
#endif

#if !defined(SHRT_MIN)
#define SHRT_MIN (-32767 - 1)
#endif
#if !defined(SHRT_MAX)
#define SHRT_MAX 32767
#endif
#if !defined(USHRT_MAX)
#define USHRT_MAX 65535
#endif

#if !defined(INT_MIN)
#define INT_MIN (-2147483647 - 1)
#endif
#if !defined(INT_MAX)
#define INT_MAX 2147483647
#endif
#if !defined(UINT_MAX)
#define UINT_MAX 4294967295u
#endif

#if !defined(LONG_MIN)
#define LONG_MIN (-9223372036854775807l - 1l)
#endif
#if !defined(LONG_MAX)
#define LONG_MAX 9223372036854775807l
#endif
#if !defined(ULONG_MAX)
#define ULONG_MAX 18446744073709551615ul
#endif

#if !defined(LLONG_MIN)
#define LLONG_MIN (-9223372036854775807ll - 1ll)
#endif
#if !defined(LLONG_MAX)
#define LLONG_MAX 9223372036854775807ll
#endif
#if !defined(ULLONG_MAX)
#define ULLONG_MAX 18446744073709551615ull
#endif

#if !defined(INT8_MIN)
#define INT8_MIN SCHAR_MIN
#endif
#if !defined(INT8_MAX)
#define INT8_MAX SCHAR_MAX
#endif
#if !defined(UINT8_MAX)
#define UINT8_MAX UCHAR_MAX
#endif

#if !defined(INT16_MIN)
#define INT16_MIN SHRT_MIN
#endif
#if !defined(INT16_MAX)
#define INT16_MAX SHRT_MAX
#endif
#if !defined(UINT16_MAX)
#define UINT16_MAX USHRT_MAX
#endif

#if !defined(INT32_MIN)
#define INT32_MIN INT_MIN
#endif
#if !defined(INT32_MAX)
#define INT32_MAX INT_MAX
#endif
#if !defined(UINT32_MAX)
#define UINT32_MAX UINT_MAX
#endif

#if !defined(INT64_MIN)
#define INT64_MIN LLONG_MIN
#endif
#if !defined(INT64_MAX)
#define INT64_MAX LLONG_MAX
#endif
#if !defined(UINT64_MAX)
#define UINT64_MAX ULLONG_MAX
#endif

#if !defined(INTPTR_MIN)
#define INTPTR_MIN LLONG_MIN
#endif
#if !defined(INTPTR_MAX)
#define INTPTR_MAX LLONG_MAX
#endif
#if !defined(UINTPTR_MAX)
#define UINTPTR_MAX ULLONG_MAX
#endif
#if !defined(PTRDIFF_MIN)
#define PTRDIFF_MIN LONG_MIN
#endif
#if !defined(PTRDIFF_MAX)
#define PTRDIFF_MAX LONG_MAX
#endif
#if !defined(SIZE_MAX)
#define SIZE_MAX ULONG_MAX
#endif

#endif /* PRIMITIVES_H */
