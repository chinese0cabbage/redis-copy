#pragma once

#include "mt19937-64.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#define DICT_OK 0
#define DICT_ERR 1

typedef struct dictEntry{
    void *key;
    union {
        void *val;
        uint64_t u64;
        int64_t s64;
        double d;
    }v;
    struct dictEntry *next;
    void *metadata[];
} dictEntry;

typedef struct dict dict;

typedef struct dictType{
    uint64_t (*hashFunction)(const void *key);
    void *(*keyDup)(dict *d, const void *key);
    void *(*valDup)(dict *d, const void *obj);
    int (*keyCompare)(dict *d, const void *key1, const void *key2);
    void (*keyDestructor)(dict *d, void *key);
    void (*valDestructor)(dict *d, void *obj);
    int (*expandAllowed)(size_t moreMem, double usedRatio);
    size_t (*dictEntryMetadataBYtes)(dict *d);
    size_t (*dictMetadataBytes)(void);
    void (*afterReplaceEntry)(dict *d, dictEntry *entry);

    uint32_t no_value:1;//by experience, no_value will ignore init value when announce
    uint32_t key_are_odd:1;
}dictType;

#define DICTHT_SIZE(exp) ((exp) == -1? 0: (uint64_t)1 << (exp))
#define DICTHT_SIZE_MASK(exp) ((exp) == -1? 0: (DICTHT_SIZE(exp)) - 1)

struct dict{
    dictType *type;

    dictEntry **ht_table[2];
    uint64_t ht_used[2];

    long reHashIdx;

    int16_t pauseRehash;
    int8_t ht_size_exp[2];

    void *metadata[];
};

typedef struct dictIterator{
    dict *d;
    long index;
    int table, safe;
    dictEntry *entry, *nextEntry;
    unsigned long long fingerPrint;
}dictIterator;

typedef void (dictScanFunction)(void *privData, const dictEntry *de);
typedef void *(dictDefragAllocFunction)(void *ptr);
typedef struct{
    dictDefragAllocFunction *defragAlloc;
    dictDefragAllocFunction *defragKey;
    dictDefragAllocFunction *defragVal;
}dictDefragAllocFunctions;

#define DICT_HT_INITIAL_EXP 2
#define DICT_HT_INITIAL_SIZE (1 << (DICT_HT_INITIAL_EXP))

#if ULONG_MAX >= 0xffffffffffffffff
#define randomULong() ((unsigned long) genrand64_int64())
#endif

typedef enum{
    DICT_RESIZE_ENABLE,
    DICT_RESIZE_AVOID,
    DICT_RESIZE_FORBID,
}dictResizeEnable;

dict *dictCreate(dictType *type);
int dictExpand(dict *d, uint64_t size);
int dictTryExpand(dict *d, uint64_t size);
void *dictMetadata(dict *d);
int dictAdd(dict *d, void *key, void *val);
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing);
void *dictFindPositionForInsert(dict *d, const void *key, dictEntry **existing);
dictEntry *dictInsertAtPosition(dict *d, void *key, void *position);
dictEntry *dictAddOrFind(dict *d, void *key);
int dictReplace(dict *d, void *key, void *value);
int dictDelete(dict *d, const void *key);
dictEntry *dictUnlink(dict *d, const void *key);
void dictFreeUnlinkedEntry(dict *d, dictEntry *he);
dictEntry *dictTwoPhaseUnlinkFind(dict *d, const void *key, dictEntry ***plink, int *table_index);
void dictRelease(dict *d);
dictEntry *dictFind(dict *d, const void *key);
void *dictFetchValue(dict *d, const void *key);
int dictResize(dict *d);
void dictSetKey(dict *d, dictEntry *de, void *key);
void dictSetVal(dict *d, dictEntry *de, void *val);
void dictSetSignedIntegerVal(dictEntry *de, int64_t val);
void dictSetUnsignedINtegerVal(dictEntry *de, uint64_t val);
void dictSetDoubleVal(dictEntry *de, double val);
int64_t dictIncrSignedIntegerVal(dictEntry *de, int64_t val);
uint64_t dictINcrUnsignedIntegerVal(dictEntry *de, uint64_t val);
double dictIncrDoubleVal(dictEntry *de, double val);
void *dictEntryMetadata(dictEntry *de);
void *dictGetKey(const dictEntry *de);
void *dictGetVal(const dictEntry *de);
int64_t dictGetSignedIntegerVal(const dictEntry *de);
uint64_t dictGetUnsignedIntegerVal(const dictEntry *de);
double dictGetDoubleVal(const dictEntry *de);
double *dictGetDoubleValPtr(dictEntry *de);
size_t dictMemUsage(const dict *d);
size_t dictEntryMemUsage(void);
dictIterator *dictGetIterator(dict *d);
dictIterator *dictGetSafeIterator(dict *d);
void dictInitIterator(dictIterator *iter, dict *d);
void dictInitSafeIterator(dictIterator *iter, dict *d);
void dictResetIterator(dictIterator *iter);
dictEntry *dictNext(dictIterator *iter);
void dictReleaseIterator(dictIterator *iter);
dictEntry *dictGetRandomKey(dict *d);
dictEntry *dictGetFairRandomKey(dict *d);
uint32_t dictGetSomeKeys(dict *d, dictEntry **des, uint32_t count);
void dictGetStats(char *buf, size_t bufSize, dict *d, int full);
uint64_t dictGenHashFunction(const void *key, size_t len);
uint64_t dictGenCaseHashFunction(const uint8_t *buf, size_t len);
void dictEmpty(dict *d, void(callback)(dict *));
void dictSetREsizeEnabled(dictResizeEnable enable);
int dictRehash(dict *d, int n);
int dictRehashMilliseconds(dict *d, int ms);
void dictSetHashFunctionSeed(uint8_t *seed);
uint8_t *dictGetHashFunctionSeed(void);
uint64_t dictScan(dict *d, uint64_t v, dictScanFunction *fn, void *privdata);
uint64_t dictScanDefrag(dict *d, uint64_t v, dictScanFunction *fn, dictDefragAllocFunctions *defragfns, void *privdata);
uint64_t dictGetHash(dict *d, const void *key);
dictEntry *dictFindEntryByPtrAndHash(dict *d, const void *oldptr, uint64_t hash);