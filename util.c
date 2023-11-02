#include "fpconv_dtoa.h"
#include "sha256.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <float.h>
#include <stdint.h>
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define UNUSED __attribute__(()unused)

static int stringmatchlen_impl(const char *pattern, int patternlen, const char *string, int stringLen, int nocase, int *skipLongerMatches){
    
}