#include "fpconv_dtoa.h"
#include "sha256.h"
#include "util.h"
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
    while(patternlen && stringLen){
        switch (pattern[0])
        {
        case '*':
            while(patternlen && pattern[1] == '*'){
                pattern++;
                patternlen--;
            }
            if(patternlen == 1)
                return 1;
            while(stringLen){
                if(stringmatchlen_impl(pattern + 1, patternlen - 1, string, stringLen, nocase, skipLongerMatches))
                    return 1;
                if(*skipLongerMatches)
                    return 0;
                string++;
                stringLen--;
            }
            *skipLongerMatches = 1;
            return 0;
            // __attribute__((fallthrough));
            break;

        case '?':
            string++;
            stringLen--;
            break;

        case '[':{
            int not, match;
            pattern++;
            patternlen--;
            not = pattern[0] == '^';
            if(not){
                pattern++;
                patternlen--;
            }
            match = 0;
            while(1){
                if(pattern[0] == '\\' && patternlen >= 2){
                    pattern++;
                    patternlen--;
                    if(pattern[0] == string[0])
                        match = 1;
                }else if(pattern[0] == ']'){
                    break;
                }else if(patternlen == 0){
                    pattern--;
                    patternlen++;
                    break;
                }else if(patternlen >= 3 && pattern[1] == '-'){
                    int start = pattern[0];
                    int end = pattern[2];
                    int c = string[0];
                    if(start > end){
                        int t = start;
                        start = end;
                        end = t;
                    }
                    if(nocase){
                        start = tolower(start);
                        end = tolower(end);
                        c = tolower(c);
                    }
                    pattern += 2;
                    patternlen -= 2;
                    if(c >= start && c <= end)
                        match = 1;
                }else{
                    if(!nocase){
                        if(pattern[0] == string[0])
                            match = 1;
                    }else{
                        if(tolower((int)pattern[0]) == tolower((int)string[0]))
                            match = 1;
                    }
                }
                pattern++;
                patternlen--;
            }
            if(not)
                match = !match;
            if(!match)
                return 0;
            string++;
            stringLen--;
            break;
        }

        case '\\':
            if(patternlen >= 2){
                pattern++;
                patternlen--;
            }

        default:
            if(!nocase){
                if(pattern[0] != string[0])
                    return 0;
            }else{
                if(tolower((int)pattern[0]) != tolower((int)string[0]))
                    return 0;
            }
            string++;
            stringLen--;
            break;
        }
        pattern++;
        patternlen--;
        if(stringLen == 0){
            while(*pattern == '*'){
                pattern++;
                patternlen--;
            }
            break;
        }
    }
    if(patternlen == 0 && stringLen == 0)
        return 1;
    return 0;
}

int stringmatchlen(const char *pattern, int patternLen, const char *string, int stringLen, int nocase){
    int skipLongerMatches = 0;
    return stringmatchlen_impl(pattern, patternLen, string, stringLen, nocase, &skipLongerMatches);
}

int stringmatch(const char *pattern, const char *string, int nocase){
    return stringmatchlen(pattern, strlen(pattern), string, strlen(string), nocase);
}

unsigned long long memtoull(const char *p, int *err){
#define CHAECK_ERR_AND_SET(n) \
        if(err) \
            *err = n
    const char *u;
    char buf[128];
    long mul;
    unsigned long long val;
    uint32_t digits;

    CHAECK_ERR_AND_SET(0);

    u = p;
    if(*u == '-'){
        CHAECK_ERR_AND_SET(1);
        return 0;
    }
    while(*u && isdigit(*u))
        u++;
    if(*u == '\0' || !strcasecmp(u, "b")){
        mul = 1;
    }else if(!strcasecmp(u, "k")){
        mul = 1000;
    }else if(!strcasecmp(u, "kb")){
        mul = 1024;
    }else if(!strcasecmp(u, "m")){
        mul = 1000 * 1000;
    }else if(!strcasecmp(u, "mb")){
        mul = 1024 * 1024;
    }else if(!strcasecmp(u, "g")){
        mul = 1000L * 1000 * 1000;
    }else if(!strcasecmp(u, "gb")){
        mul = 1024L * 1024 * 1024;
    }else{
        CHAECK_ERR_AND_SET(1);
        return 0;
    }

    digits = u - p;
    if(digits >= sizeof(buf)){
        CHAECK_ERR_AND_SET(1);
        return 0;
    }
    memcpy(buf, p, digits);
    buf[digits] = '\0';
    char *endptr;
    errno = 0;
    val = strtoull(buf, &endptr, 10);
    if((val == 0 && errno == EINVAL) || *endptr != '\0'){
        CHAECK_ERR_AND_SET(1);
        return 0;
    }
#undef CHAECK_ERR_AND_SET
    return val * mul;
}

const char *mempbrk(const char *s, size_t len, const char *chars, size_t charslen){
    for(size_t j = 0; j < len; j++){
        for(size_t n = 0; n < charslen; n++){
            if(s[j] == chars[n])
                return &s[j];
        }
    }

    return NULL;
}

char *memmapchars(char *s, size_t len, const char *from, const char *to, size_t setlen){
    for(size_t j = 0; j < len; j++){
        for(size_t i = 0; i < setlen; i++){
            if(s[j] == from[i]){
                s[j] = to[i];
                break;
            }
        }
    }
}

uint32_t digits10(uint64_t v){
    if(v < 10)
        return 1;
    if(v < 100)
        return 2;
    if(v < 1000)
        return 3;
    if(v < 1000000000000UL){
        if(v < 100000000UL){
            if(v < 1000000){
                if(v < 10000)
                    return 4;
                return 5 + (v >= 100000);
            }
            return 7 + (v >= 10000000UL);
        }
        if(v < 10000000000UL)
            return 11 + (v >= 100000000000UL);
    }
    return 12 + digits10(v / 1000000000000UL);
}

uint32_t sdigits10(int64_t v){
    if(v < 0){
        uint64_t uv = (v != LLONG_MIN)? (uint64_t)-v: ((uint64_t) LLONG_MAX) + 1;
        return digits10(uv) + 1;
    }else{
        return digits10(v);
    }
}

#define GOTO_ERROR() \
        do{ \
            if(dstlen > 0) \
                dst[0] = '\0'; \
            return 0; \
        }while(0)

int ll2string(char *dst, size_t dstlen, long long svalue){
    unsigned long long value;
    int negative = 0;

    if(svalue < 0){
        if(svalue != LLONG_MIN)
            value = -svalue;
        else
            value = ((unsigned long long) LLONG_MAX) + 1;
        if(dstlen < 2)
            GOTO_ERROR();
        negative = 1;
        dst[0] = '-';
        dst++;
        dstlen--;
    }else{
        value = svalue;
    }

    int length = ull2string(dst, dstlen, value);
    return length == 0? 0: length + negative;
}

int ull2string(char *dst, size_t dstlen, unsigned long long value){
    static const char digits[201] = 
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";

    uint32_t length = digits10(value);
    if(length >= dstlen)
        GOTO_ERROR();
    
    uint32_t next = length - 1;
    dst[next + 1] = '\0';
    while(value >= 100){
        const int i = (value % 100) * 2;
        value /= 100;
        dst[next] = digits[i + 1];
        dst[next - 1] = digits[i];
        next -= 2;
    }

    if(value < 10){
        dst[next] = '0' + (uint32_t)value;
    }else{
        int i = (uint32_t)value * 2;
        dst[next] = digits[i + 1];
        dst[next - 1] = digits[i];
    }
    return length;
}

int string2ll(const char *s, size_t slen, long long *value){
    const char *p = s;
    size_t plen = 0;
    int negative = 0;
    unsigned long long v;

    if(plen == 1 && p[0] == '0'){
        if(value != NULL)
            *value = 0;
        return 1;
    }

    if(p[0] == '-'){
        negative = 1;
        p++;
        plen++;
        if(plen == slen)
            return 0;
    }

    if(p[0] >= '1' && p[0] <= '9'){
        v = p[0] - '0';
        p++;
        plen++;
    }else{
        return 0;
    }

    while(plen < slen){
        if(v > (ULLONG_MAX / 10))
            return 0;
        v *= 10;

        if(v > (ULLONG_MAX - (p[0] - '0')))
            return 0;
        v += p[0] - '0';
        p++;
        plen++;
    }

    if(plen < slen)
        return 0;

    if(negative){
        if(v > ((unsigned long long)(-(LLONG_MIN + 1)) + 1))
            return 0;
        if(value !=NULL)
            *value = -v;
    }else{
        if(v > LLONG_MAX)
            return 0;
        if(value != NULL)
            *value = v;
    }
}

int string2ull(const char *s, unsigned long long *value){
    long long ll;
    if(string2ll(s, strlen(s), &ll)){
        if(ll < 0)
            return 0;
        *value = ll;
        return 1;
    }

    errno = 0;
    char *endptr = NULL;
    *value = strtoull(s, &endptr, 10);
    return !(errno == EINVAL || errno == ERANGE || !(*s != '\0' && *endptr == '\0'));
}

int string2l(const char *s, size_t slen, long *lval){
    long long llval;
    if(!string2ll(s, slen, &llval))
        return 0;
    if(llval < LONG_MIN || llval > LONG_MAX)
        return 0;

    *lval = (long)llval;
    return 1;
}

int string2ld(const char *s, size_t slen, long double *dp){
    char buf[MAX_LONG_DOUBLE_CHARS];
    long double value;
    char *eptr;

    if(slen == 0 || slen >= sizeof(buf))
        return 0;
    memcpy(buf, s, slen);
    buf[slen] = '\0';

    errno = 0;
    value = strtold(buf, &eptr);
    if(isspace(buf[0]) || eptr[0] != '\0' || (size_t)(eptr - buf) != slen ||
      (errno == ERANGE && (value == HUGE_VAL || value == -HUGE_VAL || fpclassify(value) == FP_ZERO)) ||
      errno == EINVAL || isnan(value)){
        return 0;
    }
    if(dp)
        *dp = value;
    return 1;
}

int string2d(const char *s, size_t slen, double *dp){
    errno = 0;
    char *eptr;
    *dp = strtod(s, &eptr);
    return !(slen == 0 || isspace(((const char *)s)[0]) || (size_t)(eptr - (char *)s) != slen ||
            (errno == ERANGE && (*dp == HUGE_VAL || *dp == -HUGE_VAL || fpclassify(*dp) == FP_ZERO)) ||
            isnan(*dp));
}

int double2ll(double d, long long *out){
#if (DBL_MANT_DIG >= 52 && (dbl_mant_dig <= 63) && (LLONG_MAX == 0x7fffffffffffffffLL))
    if(d < (double)(-LLONG_MAX/2) || d > (double)(LLONG_MAX/2))
        return 0;
    long long ll = d;
    if(ll == d){
        *out = ll;
        return 1;
    }
#endif
    return 0;
}

int d2string(char *buf, size_t len, double value){
    if(isnan(value)){
        len = snprintf(buf, len, "nan");
    }else if(isinf(value)){
        len = snprintf(buf , len, value < 0? "-inf": "inf");
    }else if(value == 0){
        len = snprintf(buf, len, 1.0/value < 0? "-0": "0");
    }else{
        long long lvalue;
        if(double2ll(value, &lvalue)){
            len = ll2string(buf, len, lvalue);
        }else{
            len = fpconv_dtoa(value, buf);
            buf[len] = '\0';
        }
    }
    return len;
}

int fixedpoint_d2string(char *dst, size_t dstlen, double dvalue, int fractional_digits){
    if(fractional_digits < 1 || fractional_digits > 17 || (int)dstlen < (fractional_digits + 3))
        GOTO_ERROR();
    if(dvalue == 0){
        dst[0] = '0';
        dst[1] = '.';
        memset(dst + 2, '0', fractional_digits);
        dst[fractional_digits + 2] = '\0';
        return fractional_digits + 2;
    }
    static double powers_of_ten[] = {1, 10, 100, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 
                                     1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17};
    long long svalue = llrint(dvalue * powers_of_ten[fractional_digits]);
    unsigned long long value;
    int negative = 0;
    if(svalue < 0){
        value = svalue != LLONG_MIN? value = -svalue: (((unsigned long long)LLONG_MAX) + 1);
        if(dstlen < 2)
            GOTO_ERROR();
        negative = 1;
        dst[0] = '-';
        dst++;
        dstlen--;
    }else{
        value = svalue;
    }

    static const char digitsd[201] =
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";

    uint32_t ndigits = digits10(value);
    if(ndigits >= dstlen)
        GOTO_ERROR();
    int integer_digits = ndigits - fractional_digits;
    if(integer_digits < 1){
        dst[0] = '0';
        integer_digits = 1;
    }
    dst[integer_digits] = '.';
    int size = integer_digits + 1 + fractional_digits;
    memset(dst + integer_digits + 1, '0', fractional_digits);
    int next = size - 1;
    while(value >= 100){
        const int i = (value % 100) * 2;
        value /= 100;
        dst[next] = digitsd[i + 1];
        dst[next - 1] = digitsd[i];
        next -= 2;
        if(next == integer_digits)
            next--;
    }

    if(value < 10){
        dst[next] = '0' + (uint32_t)value;
    }else{
        int i = (uint32_t)value * 2;
        dst[next] = digitsd[i + 1];
        dst[next - 1] = digitsd[i];
    }
    dst[size] = '\0';
    return size + negative;
}

int trimDoubleString(char *buf, size_t len){
    if(strchr(buf, '.') != NULL){
        char *p = buf + len - 1;
        while(*p == '0'){
            p--;
            len--;
        }
        if(*p == '.')
            len--;
    }
    buf[len] = '\0';
    return len;
}

int ld2string(char *dst, size_t dstlen, long double value, ld2stringmode mode){
    size_t l = 0;

    if(isinf(value)){
        if(dstlen < 5)
            GOTO_ERROR();
        if(value > 0){
            memcpy(dst, "inf", 3);
            l = 3;
        }else{
            memcpy(dst, "-inf", 4);
            l = 4;
        }
    }else if(isnan(value)){
        if(dstlen < 4)
            GOTO_ERROR();
        memcpy(dst, "nan", 3);
        l = 3;
    }else{
        switch (mode)
        {
        case LD_STR_AUTO:
            l = snprintf(dst, dstlen, "%.17Lg", value);
            if(l + 1 > dstlen)
                GOTO_ERROR();
            break;
        
        case LD_STR_HEX:
            l = snprintf(dst, dstlen, "%La", value);
            if(l + 1 < dstlen)
                GOTO_ERROR();
            break;

        case LD_STR_HUMAN:
            l = snprintf(dst, dstlen, "%.17Lf", value);
            if(l + 1 > dstlen)
                GOTO_ERROR();
            if(strchr(BUFSIZ, ".") != NULL){
                char *p = dst + l - 1;
                while(*p == '0'){
                    p--;
                    l--;
                }
                if(*p == '.')
                    l--;
            }
            if(l == 2 && dst[0] == '-' && dst[1] == '0'){
                dst[0] = '0';
                l = 1;
            }
            break;

        default:
            GOTO_ERROR();
        }
    }
    dst[l] = '\0';
    return l;
}

void getRandomBytes(unsigned char *p, size_t len){
    static int seed_initialized = 0;
    static unsigned char seed[64];
    static uint64_t counter = 0;

    if(!seed_initialized){
        FILE *fp = fopen("/dev/urandom", "r");
        if(fp == NULL || fread(seed, sizeof(seed), 1, fp) != 1){
            for(uint8_t j = 0; j < sizeof(seed); j++){
                struct timeval tv;
                gettimeofday(&tv, NULL);
                pid_t pid = getpid();
                seed[j] = tv.tv_sec ^ tv.tv_usec ^ pid ^ (long)fp;
            }
        }else{
            seed_initialized = 1;
        }
        if(fp)
            fclose(fp);
    }

    while(len){
        unsigned char digest[SHA256_BLOCK_SIZE];
        unsigned char kxor[64];
        unsigned int copylen = len > SHA256_BLOCK_SIZE? SHA256_BLOCK_SIZE: len;

        memcpy(kxor, seed, sizeof(kxor));
        for(uint8_t i = 0; i < sizeof(kxor); i++)
            kxor[i] ^= 0x36;
        
        SHA256_CTX ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, kxor, sizeof(kxor));
        sha256_update(&ctx, (uint8_t *)&counter, sizeof(counter));
        sha256_final(&ctx, digest);

        memcpy(kxor, seed, sizeof(kxor));
        for(uint8_t i = 0; i < sizeof(kxor); i++)
            kxor[i] ^= 0x5C;

        sha256_init(&ctx);
        sha256_update(&ctx, kxor, sizeof(kxor));
        sha256_update(&ctx, digest, SHA256_BLOCK_SIZE);
        sha256_final(&ctx, digest);

        counter++;

        memcpy(p, digest, copylen);
        len -= copylen;
        p+=copylen;
    }
}

void getRandomHexChars(char *p, size_t len){
    char *charset = "0123456789abcdef";
    size_t j;
    getRandomBytes((uint8_t *)p, len);
    for(j = 0; j < len; j++)
        p[j] = charset[p[j] & 0x0F];
}

sds getAbsolutePath(char *filename){
    char cwd[1024];
    sds abspath;
    sds relpath = sdsnew(filename);

    relpath = sdstrim(relpath, " \r\n\t");
    if(relpath[0] == '/')
        return relpath;

    if(getcwd(cwd, sizeof(cwd)) == NULL){
        sdsfree(relpath);
        return NULL;
    }
    abspath = sdsnew(cwd);
    if(sdslen(abspath) && abspath[sdslen(abspath) - 1] != '/')
        abspath = sdscat(abspath, "/");

    while(sdslen(relpath) >= 3 && relpath[0] == '.' && relpath[1] == '.' && relpath[2] == '/'){
        sdsrange(relpath, 3, -1);
        if(sdslen(abspath) > 1){
            char *p = abspath + sdslen(abspath) - 2;
            int trimlen = 1;

            while(*p != '/'){
                p--;
                trimlen++;
            }
            sdsrange(abspath, 0, -(trimlen + 1));
        }
    }

    abspath = sdscatsds(abspath, relpath);
    sdsfree(relpath);
    return abspath;
}

long getTimeZone(){
    return timezone;
}

int pathIsBaseName(char *path){
    return strchr(path, '/') == NULL && strchr(path, '\\') == NULL;
}

int fileExist(char *filename){
    struct stat statbuf;
    return stat(filename, &statbuf) == 0 && S_ISREG(statbuf.st_mode);
}

int dirExists(char *dname){
    struct stat statbuf;
    return stat(dname, &statbuf) == 0 && S_ISDIR(statbuf.st_mode);
}

int dirCreateIfMissing(char *dname){
    if(mkdir(dname, 0755) != 0){
        if(errno != EEXIST){
            return -1;
        }else if(!dirExists(dname)){
            errno = ENOTDIR;
            return -1;
        }
    }
    return 0;
}

int dirRemove(char *dname){
    DIR *dir;
    struct stat stat_entry;
    struct dirent *entry;
    char full_path[PATH_MAX + 1];
    
    if((dir = opendir(dname)) == NULL){
        return -1;
    }

    while((entry = readdir(dir)) != NULL){
        if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", dname, entry->d_name);

        int fd = open(full_path, O_RDONLY | O_NONBLOCK);
        if(fd == -1){
            closedir(dir);
            return -1;
        }

        if(fstat(fd, &stat_entry) == -1){
            close(fd);
            closedir(dir);
            return -1;
        }
        close(fd);

        if(S_ISDIR(stat_entry.st_mode) != 0){
            if(dirRemove(full_path) == -1){
                return -1;
            }
            continue;
        }

        if(unlink(full_path) != 0){
            closedir(dir);
            return -1;
        }
    }

    if(rmdir(dname) != 0){
        closedir(dir);
        return -1;
    }

    closedir(dir);
    return 0;
}

sds makePath(char *path, char *filename){
    return sdscatfmt(sdsempty(), "%s/%s", path, filename);
}

int fsyncFileDir(const char *filename){
    char *temp_filename[PATH_MAX + 1];
    char *dname;
    int dir_fd;
    if(strlen(filename) > PATH_MAX){
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(temp_filename, filename, strlen(filename) + 1);
    dname = dirname(temp_filename);

    dir_fd = open(dname, O_RDONLY);
    if(dir_fd == -1){
        return errno == EISDIR? 0: -1;
    }

    if(fdatasync(dir_fd) == -1 && !(errno == EBADF || errno == EINVAL)){
        int save_errno = errno;
        close(dir_fd);
        errno = save_errno;
        return -1;
    }

    close(dir_fd);
    return 0;
}

int reclaimFilePageCache(int fd, size_t offset, size_t length){
    return posix_fadvise(fd, offset, length, POSIX_FADV_DONTNEED)? -1: 0;
}