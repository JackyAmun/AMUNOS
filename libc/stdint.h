/* stdint.h -- minimal fixed-width integer types for Makar ring-3 apps.
 * The userspace build is -nostdinc, so this is our own (i686/ILP32). */
#ifndef _USERSPACE_STDINT_H
#define _USERSPACE_STDINT_H

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef short              int16_t;
typedef unsigned short     uint16_t;
typedef int                int32_t;
typedef unsigned int       uint32_t;
typedef long long          int64_t;
typedef unsigned long long uint64_t;

typedef int                intptr_t;
typedef unsigned int       uintptr_t;

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535
#define INT32_MIN  (-2147483647-1)
#define INT32_MAX  2147483647
#define UINT32_MAX 4294967295u

#endif
