#pragma once

#include <features.h>
#include <fcntl.h>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define assert(_e) (likely((_e))? (void)0: (_serverAssert(#_e, __FILE__, __LINE__), __builtin_unreachable()))
#define panic(...) _serverPanic(__FILE__, __LINE__, __VA_ARGS__), __builtin_unreachable()

void _serverAssert(const char *estr, const char *file, int line);
void _serverPanic(const char *file, int line, const char *msg, ...);