/* inttypes.h -- printf/scanf format macros for Makar apps (-nostdinc, ILP32). */
#ifndef _USERSPACE_INTTYPES_H
#define _USERSPACE_INTTYPES_H
#include "stdint.h"

#define PRId8  "d"
#define PRIu8  "u"
#define PRIx8  "x"
#define PRId16 "d"
#define PRIu16 "u"
#define PRIx16 "x"
#define PRId32 "d"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRIX32 "X"
#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIx64 "llx"

#endif
