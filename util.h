#pragma once

#include <stdint.h>
#include "sds.h"

#define MAX_LONG_DOUBLE_CHARS 5*1024

#define MAX_DOUBLE_CHARS 400

#define MAX_D2STRING_CHARS 128

#define LONG_STR_SIZE 21

typedef enum{
    LD_STR_AUTO,
    LD_STR_HUMAN,
    LD_STR_HEX
} ld2stringmode;

int stringmatchlen(const char *p, int plen, const char *s, int slen, int nocase);
int stringmatch(const char *p, const char *s, int nocase);
int stringmatchlen_fuzz_test(void);
unsigned long long memtoull(const char *p, int *err);
const char *mempbrk(const char *s, size_t len, const char *chars, size_t charslen);
char *memmapchars(char *s, size_t len, const char *from, const char *to, size_t setlen);
uint32_t digits10(uint64_t v);
uint32_t sdigits10(int64_t v);
int ll2string(char *s, size_t len, long long value);
int ull2string(char *s, size_t len, unsigned long long value);
int string2ll(const char *s, size_t slen, long long *value);
int string2ull(const char *s, unsigned long long *value);
int string2l(const char *s, size_t slen, long *value);
int string2ld(const char *s, size_t slen, long double *value);
int string2d(const char *s, size_t slen, double *dp);
int trimDoubleString(char *buf, size_t len);
int fixedpoint_d2string(char *dst, size_t dstlen, double dvalue, int fractional_digits);
int ld2string(char *buf, size_t len, long double value, ld2stringmode mode);
int double2ll(double d, long long *out);
int yesnotoi(char *s);
sds getAbsolutePath(char *filename);
long getTimeZone(void);
int pathIsBaseName(char *path);
int dirCreateIfMissing(char *dname);
int dirExists(char *dname);
int dirRemove(char *dname);
int fileExist(char *filename);
sds makePath(char *path, char *filename);
int fsyncFileDir(const char *filename);
int reclaimFilePageCache(int fd, size_t offset, size_t length);

size_t redis_strlcpy(char *dst, const char *src, size_t dsize);
size_t redis_strlcat(char *dst, const char *src, size_t dsize);