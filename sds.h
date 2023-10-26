#pragma once

#define SDS_MAX_PREALLOC (1024*1024)
extern const char *SDS_NOINIT;

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

typedef char *sds;

#define PACKED __attribute__ ((__packed__)) //stand for not aligned,making memory be more dense

struct PACKED sdshdr5{
    unsigned char flags;
    char buf[];
};

struct PACKED sdshdr8{
    uint8_t len;
    uint8_t alloc;
    uint8_t flags;
    char buf[];
};

struct PACKED sdshdr16{
    uint16_t len;
    uint16_t alloc;
    uint8_t flags;
    char buf[];
};

struct PACKED sdshdr32{
    uint32_t len;
    uint32_t alloc;
    uint8_t flags;
    char buf[];
};

struct PACKED sdshdr64{
    uint64_t len;
    uint64_t alloc;
    uint8_t flags;
    char buf[];
};

#define SDS_TYPE_5 0
#define SDS_TYPE_8 1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7
#define SDS_TYPE_BITS 3
#define SDS_HDR_VAR(T, s) struct sdshdr##T *sh = (void *)((s) - (sizeof(struct sdshdr##T)))
#define SDS_HDR(T, S) ((struct sdshdr##T *)((s) - (sizeof(struct sdshdr##T))))
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)

static inline size_t sdslen(const sds s){
    uint8_t flags = s[-1];

    switch(flags & SDS_TYPE_MASK){
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8, s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16, s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32, s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64, s)->len;
    }
    return 0;
}

static inline size_t sdsavil(const sds s){
    uint8_t flags = s[-1];
    switch(flags & SDS_TYPE_MASK){
        case SDS_TYPE_5:{
            return 0;
        }
        case SDS_TYPE_8:{
            SDS_HDR_VAR(8, s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16:{
            SDS_HDR_VAR(16, s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32:{
            SDS_HDR_VAR(32, s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64:{
            SDS_HDR_VAR(64, s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

static inline void sdssetlen(sds s, size_t newlen){
    uint8_t flags = s[-1];
    switch(flags & SDS_TYPE_MASK){
        case SDS_TYPE_5:{
            uint8_t *fp = ((uint8_t *)s) - 1;
            *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
        }
        case SDS_TYPE_8:
            SDS_HDR(8, s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16, s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32, s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64, s)->len = newlen;
            break;
    }
}

static inline void sdsinclen(sds s, size_t inc){
    uint8_t flags = s[-1];
    switch(flags & SDS_TYPE_MASK){
        case SDS_TYPE_5:{
            uint8_t *fp = ((uint8_t *)s) - 1;
            uint8_t newlen = SDS_TYPE_5_LEN(flags) + inc;
            *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
        }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8, s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16, s)->len +=inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32, s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64, s)->len += inc;
            break;
    }
}

static inline size_t sdsalloc(const sds s){
    uint8_t flags = s[-1];
    switch(flags & SDS_TYPE_MASK){
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8, s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16, s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32, s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64, s)->alloc;
    }
    return 0;
}

static inline void sdssetalloc(sds s, size_t newlen){
    uint8_t flags = s[-1];
    switch(flags & SDS_TYPE_MASK){
        case SDS_TYPE_5:
            break;
        case SDS_TYPE_8:
            SDS_HDR(8, s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16, s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32, s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64, s)->alloc = newlen;
            break;
    }
}

sds sdsnewlen(const void *init, size_t initlen);
sds sdstrynewlen(const void *init, size_t initlen);
sds sdsnew(const char *init);
sds sdsempty(void);
sds sdsdup(const sds s);
void sdsfree(sds s);
sds sdsgrowzero(sds s, size_t len);
sds sdscatlen(sds s, const void *t, size_t len);
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);