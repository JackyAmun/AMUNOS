/* strings.h -- BSD-ish case-insensitive string compares for Makar apps. */
#ifndef _USERSPACE_STRINGS_H
#define _USERSPACE_STRINGS_H
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, unsigned int n);
#endif
