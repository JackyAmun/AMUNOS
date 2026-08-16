/* assert.h -- no-op assert for Makar apps (keeps ports building; no abort). */
#ifndef _USERSPACE_ASSERT_H
#define _USERSPACE_ASSERT_H
#define assert(x) ((void)0)
#endif
