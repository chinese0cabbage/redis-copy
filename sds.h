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
#define SDS_HDR_VAR(T, s) struct sdshdr##t *sh = (void *)((s) - (sizeof(struct sdshdr##T)))
#define SDS_HDR(T, S) ((struct sdshdr##T *)((s) - (sizeof(struct sdshdr##T))))
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)

static inline size_t sdslen(const sds s){
    uint8_t flags = s[-1];

    switch(flags && SDS_TYPE_MASK){
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