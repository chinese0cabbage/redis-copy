#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <limits.h>

#include "sds.h"
#include "util.h"
#include "zmalloc.h"

const char *SDS_NOTINIT = "SDS_NOINIT";

static inline int sdsHdrSize(char type){
    switch(type & SDS_TYPE_MASK){
        case SDS_TYPE_5:
            return sizeof(struct sdshdr5);
        case SDS_TYPE_8:
            return sizeof(struct sdshdr8);
        case SDS_TYPE_16:
            return sizeof(struct sdshdr16);
        case SDS_TYPE_32:
            return sizeof(struct sdshdr32);
        case SDS_TYPE_64:
            return sizeof(struct sdshdr64);
    }
    return 0;
}

static inline char sdsReqTYpe(size_t string_size){
    if(string_size < 1 << 5)
        return SDS_TYPE_5;
    if(string_size < 1 << 8)
        return SDS_TYPE_8;
    if(string_size < 1 << 16)
        return SDS_TYPE_16;
    if(string_size < 1ll << 32)
        return SDS_TYPE_32;
    return SDS_TYPE_64;
}

static inline size_t sdsTypeMaxSize(char type){
    if(type == SDS_TYPE_5)
        return (1 << 5) - 1;
    if(type == SDS_TYPE_8)
        return (1 << 8) - 1;
    if(type == SDS_TYPE_16)
        return (1 << 16) - 1;
    if(type == SDS_TYPE_32)
        return (1ll << 32) - 1;
    return -1;//64 bit signed -1 is equal to unsigned max
}

static sds _sdsnewlen(const void *init, size_t initlen, int trymalloc){
    void *sh;
    sds s;
    char type = sdsReqTYpe(initlen);

    if(type == SDS_TYPE_5 && initlen == 0)
        type = SDS_TYPE_8;
    int hdrlen = sdsHdrSize(type);
    unsigned char *fp;
    size_t usable;

    assert(initlen + hdrlen + 1 > initlen);//overflow check
    sh = trymalloc? ztrymalloc_usable(hdrlen + initlen + 1, &usable):
                    zmalloc_usable(hdrlen + initlen + 1, &usable);
    
    if(sh == NULL)
        return NULL;
    if(init == SDS_NOTINIT)
        init = NULL;
    else if(!init)
        memset(sh, 0, hdrlen + initlen + 1);
    
    s = (char *)sh + hdrlen;
    fp = ((unsigned char *)s) - 1;
    usable -=(hdrlen + 1);

    if(usable > sdsTypeMaxSize(type))
        usable = sdsTypeMaxSize(type);
    
    switch(type){
        case SDS_TYPE_5:{
            *fp = type | (initlen << SDS_TYPE_BITS);
            break;
        }

        case SDS_TYPE_8:{
            SDS_HDR_VAR(8, s);
            sh->len = initlen;
            sh->alloc = usable;
            *fp = type;
            break;
        }

        case SDS_TYPE_16:{
            SDS_HDR_VAR(16, s);
            sh->len = initlen;
            sh->alloc = usable;
            *fp = type;
            break;
        }

        case SDS_TYPE_32:{
            SDS_HDR_VAR(32, s);
            sh->len = initlen;
            sh->alloc = usable;
            *fp = type;
            break;
        }

        case SDS_TYPE_64:{
            SDS_HDR_VAR(64, s);
            sh->len = initlen;
            sh->alloc = usable;
            *fp = type;
            break;
        }
    }

    if(initlen && init)
        memcpy(s, init, initlen);
    s[initlen] = '\0';
    return s;
}

sds sdsnewlen(const void *init, size_t initlen){
    return _sdsnewlen(init, initlen, 0);
}

sds sdstrynewlen(const void *init, size_t initlen){
    return _sdsnewlen(init, initlen, 1);
}

sds sdsempty(void){
    return sdsnewlen("", 0);
}

sds sdsnew(const char *init){
    size_t initlen = (init == NULL)? 0: strlen(init);
    return sdsnewlen(init, initlen);
}

sds sdsdup(const sds s){
    return sdsnewlen(s, sdslen(s));
}

void sdsfree(sds s){
    if(s == NULL)
        return;
    zfree((char *)s - sdsHdrSize(s[-1]));
}

void sdsupdatelen(sds s){
    sdssetlen(s, strlen(s));
}

void sdsclear(sds s){
    sdssetlen(s, 0);
    s[0] = '\0';
}

static _sdsMakeRoomFor(sds s, size_t addlen, int greedy){
    void *sh, *newsh;
    size_t avail = sdsavil(s);
    size_t len, newlen, reqlen;
    char type, oldtype = s[-1] & SDS_TYPE_MASK;
    int hdrlen;
    size_t usable;

    if(avail >= addlen)
        return s;

    len = sdslen(s);
    sh = (char *)s - sdsHdrSize(oldtype);
    reqlen = newlen = (len + addlen);
    assert(newlen > len);

    if(greedy == 1){
        if(newlen < SDS_MAX_PREALLOC)
            newlen *= 2;
        else
            newlen += SDS_MAX_PREALLOC;
    }

    type = sdsReqTYpe(newlen);

    if(type == SDS_TYPE_5)
        type = SDS_TYPE_8;
    
    hdrlen = sdsHdrSize(type);
    assert(hdrlen + newlen + 1 > reqlen);
    if(oldtype == type){
        newsh = zrealloc_usable(sh, hdrlen + newlen + 1, &usable);
        if(newsh == NULL)
            return NULL;
        s = (char *)newsh + hdrlen;
    }else{
        newsh = zmalloc_usable(hdrlen + newlen + 1, &usable);
        if(newsh == NULL)
            return NULL;
        memcpy((char *)newsh + hdrlen, s, len + 1);
        zfree(sh);
        s = (char *)newsh + hdrlen;
        s[-1] = type;
        sdssetlen(s, len);
    }

    usable -= (hdrlen + 1);
    if(usable > sdsTypeMaxSize(type))
        usable = sdsTypeMaxSize(type);
    sdssetalloc(s, usable);
    return s;
}

sds sdsMakeRoomFor(sds s, size_t addlen){
    return _sdsMakeRoomFor(s, addlen, 1);
}

sds sdsMakeRoomForNonGreedy(sds s, size_t addlen){
    return _sdsMakeRoomFor(s, addlen, 0);
}

sds sdsRemoveFreeSpace(sds s, int would_regrow){
    return sdsResize(s, sdslen(s), would_regrow);
}

sds sdsResize(sds s, size_t size, int would_regrow){
    void *sh, *newsh;
    char type, oldtype = s[-1] & SDS_TYPE_MASK;
    int hdrlen, oldhdrlen = sdsHdrSize(oldtype);
    size_t len = sdslen(s);
    sh = (char *)s - oldhdrlen;

    if(sdsalloc(s) == size)
        return s;
    
    if(size < len)
        len = size;
    
    type = sdsReqTYpe(size);
    if(would_regrow && type == SDS_TYPE_5)
        type = SDS_TYPE_8;
    
    hdrlen = sdsHdrSize(type);
    int use_realloc = (oldtype == type || (type < oldtype && type > SDS_TYPE_8));
    size_t newlen = use_realloc? oldhdrlen + size + 1: hdrlen + size + 1;
    if(use_realloc){
        newsh = zrealloc(sh, newlen);
        if(newsh == NULL)
            return NULL;
        s = (char *)newsh + oldhdrlen;
    }else{
        newsh = zmalloc(newlen);
        if(newsh == NULL)
            return NULL;
        memcpy((char *)newsh + hdrlen, s, len);
        zfree(sh);
        s = (char *)newsh + hdrlen;
        s[-1] = type;
    }
    s[len] = 0;
    sdssetlen(s, len);
    sdssetalloc(s, size);
    return s;
}

size_t sdsAllocSize(sds s){
    size_t alloc = sdsalloc(s);
    return sdsHdrSize(s[-1]) + alloc + 1;
}

void *sdsAllocPtr(sds s){
    return (void *)(s - sdsHdrSize(s[-1]));
}

void sdsIncrLen(sds s, ssize_t incr){
    uint8_t flags = s[-1];
    size_t len;
    switch(flags & SDS_TYPE_MASK){
        case SDS_TYPE_5:{
            uint8_t *fp = ((uint8_t *)s) - 1;
            uint8_t oldlen = SDS_TYPE_5_LEN(flags);
            assert((incr > 0 && oldlen + incr < 32) || (incr < 0 && oldlen >= (uint32_t)(-incr)));
            *fp = SDS_TYPE_5 | ((oldlen + incr) << SDS_TYPE_BITS);
            len = oldlen + incr;
            break;
        }

        case SDS_TYPE_8:{
            SDS_HDR_VAR(8, s);
            assert((incr >= 0 && sh->alloc - sh->len >= incr) || (incr < 0 && sh->len >= (uint32_t)(-incr)));
            len = (sh->len += incr);
            break;
        }

        case SDS_TYPE_16:{
            SDS_HDR_VAR(16, s);
            assert((incr >= 0 && sh->alloc - sh->len >= incr) || (incr < 0 && sh->len >= (uint32_t)(-incr)));
            len = (sh->len += incr);
            break;
        }

        case SDS_TYPE_32:{
            SDS_HDR_VAR(32, s);
            assert((incr >= 0 && sh->alloc - sh->len >= (uint32_t)incr) || (incr < 0 && sh->len >= (uint32_t)(-incr)));
            len = (sh->len += incr);
            break;
        }

        case SDS_TYPE_64:{
            SDS_HDR_VAR(64, s);
            assert((incr >= 0 && sh->alloc - sh->len >= (uint64_t)incr) || (incr < 0 && sh->len >= (uint64_t)(-incr)));
            len = (sh->len += incr);
            break;
        }
        default:
            len = 0;
    }
    s[len] = '\0';
}

sds sdsgrowzero(sds s, size_t len){
    size_t curlen = sdslen(s);

    if(len < curlen)
        return s;
    s = sdsMakeRoomFor(s, len - curlen);
    if(s == NULL)
        return NULL;

    memset(s + curlen, 0, (len - curlen + 1));
    sdssetlen(s, len);
    return s;
}

sds sdscatlen(sds s, const void *t, size_t len){
    size_t curlen = sdslen(s);

    s = sdsMakeRoomFor(s, len);
    if(s == NULL)
        return NULL;
    memcpy(s + curlen, t, len);
    sdssetlen(s, curlen + len);
    s[curlen + len] = '\0';
    return s;
}

sds sdscat(sds s, const char *t){
    return sdscatlen(s, t, strlen(t));
}

sds sdscatsds(sds s, const sds t){
    return sdscatlen(s, t, sdslen(t));
}

sds sdscpylen(sds s, const char *t, size_t len){
    if(sdsalloc(s) < len){
        s = sdsMakeRoomFor(s, len - sdslen(s));
        if(s == NULL)
            return NULL;
    }
    memcpy(s, t, len);
    s[len] = '\0';
    sdssetlen(s, len);
    return s;
}

sds sdscpy(sds s, const char *t){
    return sdscpylen(s, t, strlen(t));
}

sds sdsfromlonglong(long long value){
    char buf[LONG_STR_SIZE];
    int len = ll2string(buf, sizeof(buf), value);

    return sdsnewlen(buf, len);
}

sds sdscatvprintf(sds s, const char *fmt, va_list ap){
    va_list cpy;
    char staticbuf[1024], *buf = staticbuf, *t;
    size_t buflen = strlen(fmt) * 2;
    int bufstrlen;

    if(buflen > sizeof(staticbuf)){
        buf = zmalloc(buflen);
        if(buf == NULL)
            return NULL;
    }else{
        buflen = sizeof(staticbuf);
    }

    while(1){
        va_copy(cpy, ap);
        bufstrlen = vsnprintf(buf, buflen, fmt, cpy);
        va_end(cpy);
        if(bufstrlen < 0){
            if(buf != staticbuf)
                zfree(buf);
            return NULL;
        }

        if(((size_t)bufstrlen) >= buflen){
            if(buf != staticbuf)
                zfree(buf);
            buflen = ((size_t)bufstrlen) + 1;
            buf = zmalloc(buflen);
            if(buf == NULL)
                return NULL;
            continue;
        }
        break;
    }

    t = sdscatlen(s, buf, bufstrlen);
    if(buf != staticbuf)
        zfree(buf);
    return t;
}

sds sdscatprintf(sds s, const char *fmt, ...){
    va_list ap;
    char *t;
    va_start(ap, fmt);
    t = sdscatvprintf(s, fmt, ap);
    va_end(ap);
    return t;
}

sds sdscatfmt(sds s, char const *fmt, ...){
    size_t initlen = sdslen(s);
    const char *f = fmt;
    long i;
    va_list ap;

    s = sdsMakeRoomFor(s, strlen(fmt) * 2);
    va_start(ap, fmt);
    f = fmt;
    i = initlen;
    while(*f){
        char next, *str;
        size_t l;
        long long num;
        unsigned long long unum;

        if(sdsavil(s) == 0){
            s = sdsMakeRoomFor(s, l);
        }

        switch(*f){
            case '%':{
                next = *(f + 1);
                if(next == '\0')
                    break;
                f++;
                switch(next){
                    case 's':__attribute__((fallthrough))
                    case 'S':{
                        str = va_arg(ap, char *);
                        l = (next == 's')? strlen(str): sdslen(str);
                        if(sdsavil(s) < l)
                            s = sdsMakeRoomFor(s, l);
                        memcpy(s + i, str, l);
                        sdsinclen(s, l);
                        i += l;
                        break;
                    }

                    case 'i':__attribute__((fallthrough))
                    case 'I':{
                        num = next == 'i'? va_arg(ap, int): va_arg(ap, long long);
                        char buf[LONG_STR_SIZE];
                        l = ll2string(buf, sizeof(buf), num);
                        if(sdsavil(s) < l)
                            s = sdsMakeRoomFor(s, l);
                        memcpy(s + i, buf, l);
                        sdsinclen(s, l);
                        i += l;
                        break;
                    }

                    case 'u':__attribute__((fallthrough))
                    case 'U':{
                        num = next == 'u'? va_arg(ap, uint32_t): va_arg(ap, unsigned long long);
                        char buf[LONG_STR_SIZE];
                        l = ull2string(buf, sizeof(buf), unum);
                        if(sdsavil(s) < l)
                            s = sdsMakeRoomFor(s, l);
                        memcpy(s + i, buf, l);
                        i += l;
                    }

                    default:
                        s[i++] = next;
                        sdsinclen(s, l);
                        break;
                }
                break;
            }

            default:
                s[i++] = *f;
                sdsinclen(s, 1);
                break;
        }
        f++;
    }
    va_end(ap);
    s[i] = '\0';
    return s;
}

sds sdstrim(sds s, const char *cset){
    char *end, *sp, *ep;
    size_t len;

    sp = s;
    ep = end = s + sdslen(s) - 1;
    while(sp <= end && strchr(cset, *sp))
        sp++;
    while(ep > sp && strchr(cset, *ep))
        ep--;
    len = (ep - sp) + 1;
    if(s != sp)
        memmove(s, sp, len);
    s[len] = '\0';
    sdssetlen(s, len);
    return s;
}

void sdssubstr(sds s,size_t start, size_t len){
    size_t oldlen = sdslen(s);
    if(start >= oldlen)
        start = len = 0;
    if(len > oldlen - start)
        len = oldlen - start;

    if(len)
        memmove(s, s + start, len);
    s[len] = 0;
    sdssetlen(s, len);
}

void sdsrange(sds s, ssize_t start, ssize_t end){
    size_t newlen, len = sdslen(s);
    if(len != 0){
        if(start < 0)
            start = len + start;
        if(end < 0)
            end = len + end;
        newlen = (start > end)? 0: (end - start) + 1;
        sdssubstr(s, start, newlen);
    }
}

void sdstolower(sds s){
    size_t len = sdslen(s);

    for (size_t i = 0; i < len; i++)
    {
        s[i] = tolower(s[i]);
    }
}

void sdstoupper(sds s){
    size_t len = sdslen(s);

    for (size_t i = 0; i < len; i++)
    {
        s[i] = toupper(s[i]);
    }
}

int sdscmp(const sds s1, const sds s2){
    size_t l1 = sdslen(s1);
    size_t l2 = sdslen(s2);
    size_t minlen = (l1 < l2)? l1: l2;
    int cmp = memcmp(s1, s2, minlen);
    if(cmp == 0)
        return l1 > l2? 1: (l1 < l2? -1: 0);
    return cmp;
}

sds *sdssplitlen(const char *s, ssize_t len, const char *sep, int seplen, int *count){
#define CLEANUP() do{ \
                    for(int j = 0; j < element; j++) \
                        sdsfree(token[j]); \
                    zfree(token); \
                    *count = 0; \
                    return NULL; \
                }while(0)

    int element = 0, slots = 5;
    long start = 0;

    if(seplen < 1 || len <= 0){
        *count = 0;
        return NULL;
    }

    sds *token = zmalloc(sizeof(sds) * slots);
    if(token == NULL)
        return NULL;

    for (size_t i = 0; i < (len - (seplen - 1)); i++)
    {
        if(slots < element + 2){
            slots *= 2;
            sds *newtokens = zrealloc(token, sizeof(sds) * slots);
            if(newtokens == NULL)
                CLEANUP();
            token = newtokens;
        }

        if((seplen == 1 && *(s + i) == sep[0]) || (memcmp(s + i, sep, seplen) == 0)){
            token[element] = sdsnewlen(s + start, i - start);
            if(token[element] == NULL)
                CLEANUP();
            element++;
            start = i + seplen;
            i += (seplen - 1);
        }
    }
    
    token[element] = sdsnewlen(s + start, len - start);
    if(token[element] == NULL)
        CLEANUP();
    element++;
    *count = element;
    return token;
#undef CLEANUP()
}

void sdsfreesplitres(sds *tokens, int count){
    if(tokens){
        while(count)
            sdsfree(tokens[count]);
        zfree(tokens);
    }
}

sds sdscatrepr(sds s, const char *p, size_t len){
    s = sdsMakeRoomFor(s, len + 2);
    s = sdscatlen(s, "\"", 1);
    while(len--){
        switch(*p){
            case '\\':__attribute__((fallthrough))
            case '"':
                s = sdscatprintf(s, "\\%c", *p);
                break;
            
            case '\r':
                s = sdscatlen(s, "\\r", 2);
                break;

            case '\t':
                s = sdscatlen(s, "\\t", 2);
                break;

            case '\a':
                s = sdscatlen(s, "\\a", 2);
                break;

            case '\b':
                s = sdscatlen(s, "\\b", 2);
                break;

            default:
                if(isprint(*p))
                    s = sdscatlen(s, p, 1);
                else
                    s = sdscatprintf(s, "\\x%02x", (uint8_t)*p);
                break;
        }
        p++;
    }
    return sdscatlen(s, "\"", 1);
}

int sdsneedsrepr(const sds s){
    size_t len = sdslen(s);
    const char *p = s;

    while(len--){
        if(*p == '\\' || *p == '"' || *p == '\n' || *p == '\r' || *p == '\t' || *p == '\a' ||
           *p == '\b' || !isprint(*p) || isspace(*p)){
           return 1;
        }
        p++;
    }
    return 0;
}

int is_hex_digit(char c){
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hex_digit_to_int(char c){
    switch(c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    default: return 0;
    }
}

sds *sdssplitargs(const char *line, int *argc){
#define GOTO_ERROR do{ while((*argc)--) sdsfree(vector[*argc]); \
                   zfree(vector); \
                   if(current) sdsfree(current); \
                   *argc = 0; \
                   return NULL; \
                   }while(1)
    const char *p = line;
    char *current = NULL;
    char **vector = NULL;

    *argc = 0;
    while(1){
        while(*p && isspace(*p))
            p++;
        
        if(*p){
            int inq = 0, insq = 0, done = 0;
            if(current == NULL)
                current = sdsempty();
            
            while(!done){
                if(inq){
                    if(*p == '\\' && *(p + 1) == 'x' && is_hex_digit(*(p + 2)) && is_hex_digit(*(p + 3))){
                        uint8_t byte = (hex_digit_to_int(*(p + 2)) * 16) + hex_digit_to_int(*(p + 3));
                        current = sdscatlen(current, (char *)&byte, 1);
                        p += 3;
                    }else if(*p == '\\' && *(p + 1)){
                        char c;
                        p++;
                        switch (*p){
                            case 'n': c = '\n'; break;
                            case 'r': c = '\r'; break;
                            case 't': c = '\t'; break;
                            case 'b': c = '\b'; break;
                            case 'a': c = '\a'; break;
                            default: c = *p; break;
                        }
                        current = sdscatlen(current, &c, 1);
                    }else if(*p == '"'){
                        if(*(p + 1) && !isspace(*(p + 1)))
                            GOTO_ERROR;
                        done = 1;
                    }else if(!*p){
                        GOTO_ERROR;
                    }else{
                        current = sdscatlen(current, p, 1);
                    }
                }else if(insq){
                    if(*p == '\\' && *(p + 1) == '\''){
                        p++;
                        current = sdscatlen(current, "'", 1);
                    }else if(*p == '\''){
                        if(*(p + 1) && !isspace(*(p + 1)))
                            GOTO_ERROR;
                        done = 1;
                    }else if(!*p){
                        GOTO_ERROR;
                    }else{
                        current = sdscatlen(current, p, 1);
                    }
                }else{
                    switch(*p){
                        case ' ':__attribute__((fallthrough))
                        case '\n':__attribute__((fallthrough))
                        case '\r':__attribute__((fallthrough))
                        case '\t':__attribute__((fallthrough))
                        case '\0':__attribute__((fallthrough))
                            done = 1;
                            break;

                        case '"':
                            inq = 1;
                            break;

                        case '\'':
                            insq = 1;
                            break;

                        default:
                            current = sdscatlen(current, p, 1);
                            break;
                    }
                }
                if(*p)
                    p++;
            }
            vector = zrealloc(vector, ((*argc) + 1) * sizeof(char *));
            vector[*argc] = current;
            (*argc)++;
            current = NULL;
        }else{
            if(vector == NULL)
                vector = zmalloc(sizeof(void *));
            return vector;
        }
    }
#undef GOTO_ERROR
}

sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen){
    size_t l = sdslen(s);

    for (size_t j = 0; j < l; j++){
        for (size_t i = 0; i < setlen; i++){
            if(s[j] == from[i]){
                s[j] = to[i];
                break;
            }
        }
    }
    return s;
}

sds sdsjoin(char **argv, int argc, char *sep){
    sds join = sdsempty();
    for (size_t i = 0; i < argc; i++){
        join = sdscat(join, argv[i]);
        if(i != argc - 1)
            join = sdscat(join, sep);
    }
    return join;
}

sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen){
    sds join = sdsempty();

    for (size_t i = 0; i < argc; i++){
        join = sdscatsds(join, argv[i]);
        if(i != argc - 1)
            join = sdscatlen(join, sep, seplen);
    }
    return join;
}

void *sds_malloc(size_t size){
    return zmalloc(size);
}

void *sds_realloc(void *ptr, size_t size){
    return zrealloc(ptr, size);
}

void sds_free(void *ptr){
    zfree(ptr);
}

sds sdstemplate(const char *template, sdstemplate_callback_t cb_func, void *cb_arg){
    sds res = sdsempty();
    const char *p = template;

    while(*p){
        const char *sv = strchr(p, '{');
        if(!sv){
            res = sdscat(res, p);
            break;
        }else if(sv > p){
            res = sdscatlen(res, p, sv - p);
        }

        sv++;
        if(!*sv){
            sdsfree(res);
            return NULL;
        }

        if(*sv == '{'){
            p = sv + 1;
            res = sdscat(res, "{");
            continue;
        }

        const char *ev = strchr(sv, '}');
        if(!ev){
            sdsfree(res);
            return NULL;
        }

        sds varname = sdsnewlen(sv, ev - sv);
        sds value = cb_func(varname, cb_arg);
        sdsfree(varname);
        if(!value){
            sdsfree(res);
            return NULL;
        }

        res = sdscat(res, value);
        sdsfree(value);
        p = ev + 1;
    }
    return res;
}