#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "redisassert.h"

void _serverAssert(const char *estr, const char *file, int line){
    fprintf(stderr, "=== ASSERTION FAILED ===");
    fprintf(stderr, "==> %s:%d '%s' is not true", file, line, estr);
    raise(SIGSEGV);
}

void _serverPanic(const char *file, int line, const char *msg, ...){
    fprintf(stderr, "-------------------------------------------------");
    fprintf(stderr, "!!! Software Failure. Press left mouse button to continue");
    fprintf(stderr, "Guru Meditation: %s #%s:%d", msg, file, line);
    abort();
}