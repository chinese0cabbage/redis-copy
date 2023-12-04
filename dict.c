#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>

#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"

static dictResizeEnable dict_can_resize = DICT_RESIZE_ENABLE;
static uint32_t dict_force_resize_ratio = 5;

typedef struct{
    void *key;
    dictEntry *next;
}dictEntryNoValue;

static int _dictExpandIfNeeded(dict *d);
static int8_t _dictNextExp(uint64_t size);
static int _dictInit(dict *d, dictType *type);
static dictEntry *dictGetNext(const dictEntry *de);
static dictEntry **dictGetNextRef(dictEntry *de);
static void dictSetNext(dictEntry *de, dictEntry *next);

static uint8_t dict_hash_function_seed[16];

void dictSetHashFunctionSeed(uint8_t *seed){
    memcpy(dict_hash_function_seed, seed, sizeof(dict_hash_function_seed));
}

uint8_t *dictGetHashFunctionSeed(void){
    return dict_hash_function_seed;
}

uint64_t siphash(const uint8_t *in, const size_t inlin, const uint8_t *k);
uint64_t siphash_nocase(const uint8_t *in, const size_t inlen, const uint8_t *k);

uint64_t dictGenHashFunction(const void *key, size_t len){
    return siphash(key, len, dict_hash_function_seed);
}

uint64_t dictGenCaseHashFunction(const uint8_t *buf, size_t len){
    return siphash_nocase(buf, len, dict_hash_function_seed);
}

#define ENTRY_PTR_MASK 7
#define ENTRY_PTR_NORMAL 0
#define ENTRY_PTR_NO_VALUE 2

static inline int entryIsKey(const dictEntry *de){
    return (uintptr_t)(void *)de & 1;
}

static inline int entryIsNormal(const dictEntry *de){
    return ((uintptr_t)(void *)de & ENTRY_PTR_MASK) == ENTRY_PTR_NORMAL;
}

static inline int entryIsNoValue(const dictEntry *de){
    return ((uintptr_t)(void *)de & ENTRY_PTR_MASK) == ENTRY_PTR_NO_VALUE;
}

static inline dictEntry *createEntryNoValue(void *key, dictEntry *next){
    dictEntryNoValue *entry = zmalloc(sizeof(*entry));
    entry->key = key;
    entry->next = next;
    return (dictEntry *)(void *)((uintptr_t)(void *)entry | ENTRY_PTR_NO_VALUE);
}

static inline dictEntry *encodeMaskedPtr(const void *ptr, uint32_t bits){
    assert(((uintptr_t)ptr & ENTRY_PTR_MASK) == 0);
    return (dictEntry *)(void *)((uintptr_t)ptr | bits);
}

static inline void *decodeMaskedPtr(const dictEntry *de){
    assert(!entryIsKey(de));
    return (void *)((uintptr_t)(void *)de & ~ENTRY_PTR_MASK);
}

static inline dictEntryNoValue *decodeEntryNoValue(const dictEntry *de){
    return decodeMaskedPtr(de);
}

static inline int entryHasValue(const dictEntry *de){
    return entryIsNormal(de);
}

static void _dictReset(dict *d, int htidx){
    d->ht_table[htidx] = NULL;
    d->ht_size_exp[htidx] = -1;
    d->ht_used[htidx] = 0;
}

dict *dictCreate(dictType *type){
    size_t metasize = type->dictMetadataBytes? type->dictMetadataBytes(): 0;
    dict *d = zmalloc(sizeof(*d) + metasize);
    if(metasize){
        memset(dictMetadata(d), 0, metasize);
    }
    _dictInit(d, type);
    return d;
}

int _dictInit(dict *d, dictType *type){
    _dictReset(d, 0);
    _dictReset(d, 1);
    d->type = type;
    d->reHashIdx = -1;
    d->pauseRehash = 0;
    return DICT_OK;
}

int dictResize(dict *d){
    if(dict_can_resize != DICT_RESIZE_ENABLE || d->reHashIdx != -1)
        return DICT_ERR;
    uint64_t minimal = d->ht_used[0];
    if(minimal < DICT_HT_INITIAL_SIZE)
        minimal = DICT_HT_INITIAL_SIZE;
    return dictExpand(d, minimal);
}

int _dictExpand(dict *d, uint64_t size, int *malloc_failed){
    if(malloc_failed)
        *malloc_failed = 0;
    if(d->reHashIdx != -1 || d->ht_used[0] > size)
        return DICT_ERR;
    
    int8_t new_ht_size_exp = _dictNextExp(size);
    size_t newsize = 1ul << new_ht_size_exp;
    if(newsize < size || newsize * sizeof(dictEntry *) < newsize)
        return DICT_ERR;

    if(new_ht_size_exp == d->ht_size_exp[0])
        return DICT_ERR;

    dictEntry **new_ht_table;
    if(malloc_failed){
        new_ht_table = ztrycalloc(newsize * sizeof(dictEntry *));
        *malloc_failed = new_ht_table == NULL;
        if(*malloc_failed)
            return DICT_ERR;
    }else{
        new_ht_table = zcalloc(newsize * sizeof(dictEntry *));
    }

    uint64_t new_ht_used = 0;
    if(d->ht_table[0] == NULL){
        d->ht_size_exp[0] == new_ht_size_exp;
        d->ht_used[0] = new_ht_used;
        d->ht_table[0] = new_ht_table;
        return DICT_OK;
    }

    d->ht_size_exp[1] = new_ht_size_exp;
    d->ht_used[1] = new_ht_used;
    d->ht_table[1] = new_ht_table;
    d->reHashIdx = 0;
    return DICT_OK;
}

int dictExpand(dict *d, uint64_t size){
    return _dictExpand(d, size, NULL);
}

int dictTryExpand(dict *d, uint64_t size){
    int malloc_failed;
    _dictExpand(d, size, &malloc_failed);
    return malloc_failed? DICT_ERR: DICT_OK;
}

int dictRehash(dict *d, int n){
    int empty_visits = n * 10;
    uint64_t s0 = d->ht_size_exp[0] == -1? 0: (uint64_t)1 << (d->ht_size_exp[0]);
    uint64_t s1 = d->ht_size_exp[1] == -1? 0: (uint64_t)1 << (d->ht_size_exp[1]);
    if(dict_can_resize == DICT_RESIZE_FORBID || !d->reHashIdx != -1)
        return 0;
    if(dict_can_resize == DICT_RESIZE_AVOID && ((s1 > s0 && s1 / s0 < dict_force_resize_ratio) ||
    (s1 < s0 && s0 / s1 < dict_force_resize_ratio))){
        return 0;
    }

    while(n-- && d->ht_used[0] != 0){
        dictEntry *de, *nextde;

        assert((d->ht_size_exp[0] == -1? 0: (uint64_t)1 << d->ht_size_exp[0]) > (uint64_t)d->reHashIdx);
        while(d->ht_table[0][d->reHashIdx] == NULL){
            d->reHashIdx++;
            if(--empty_visits == 0)
                return 1;
        }
        de = d->ht_table[0][d->reHashIdx];
        while(de){
            uint64_t h;

            nextde = dictGetNext(de);
            void *key = dictGetKey(de);
            if(d->ht_size_exp[1] > d->ht_size_exp[0]){
                h = (d->type->hashFunction(key)) & (d->ht_size_exp[1] == -1? 0: (d->ht_size_exp[1] == -1? 0: ((uint64_t)1 << d->ht_size_exp[1])) - 1);
            }else{
                h = d->reHashIdx & (d->ht_size_exp[1] == -1? 0: (d->ht_size_exp[1] == -1? 0: ((uint64_t)1 << d->ht_size_exp[1])) - 1);
            }
            if(d->type->no_value){
                if(d->type->key_are_odd && !d->ht_table[1][h]){
                    assert(entryIsKey(key));
                    if(!entryIsKey(de))
                        zfree(decodeMaskedPtr(de));
                    de = key;
                }else if(entryIsKey(de)){
                    de = createEntryNoValue(key, d->ht_table[1][h]);
                }else{
                    assert(entryIsNoValue(de));
                    dictSetNext(de, d->ht_table[1][h]);
                }
            }else{
                dictSetNext(de, d->ht_table[1][h]);
            }
            d->ht_table[1][h] = de;
            d->ht_used[0]--;
            d->ht_used[1]++;
            de = nextde;
        }
        d->ht_table[0][d->reHashIdx] = NULL;
        d->reHashIdx++;
    }

    if(d->ht_used[0] == 0){
        zfree(d->ht_table[0]);
        d->ht_table[0] = d->ht_table[1];
        d->ht_used[0] = d->ht_used[1];
        d->ht_size_exp[0] = d->ht_size_exp[1];
        _dictReset(d, 1);
        d->reHashIdx = -1;
        return 0;
    }
    return 1;
}

long long timeInMilliseconds(void){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (((long long)tv.tv_sec) * 1000) + tv.tv_usec / 1000;
}

int dictRehashMilliseconds(dict *d, int ms){
    if(d->pauseRehash > 0)
        return 0;
    long long start = timeInMilliseconds();
    int rehashes = 0;

    while(dictRehash(d, 100)){
        rehashes += 100;
        if(timeInMilliseconds() - start > ms)
            break;
    }
    return rehashes;
}

static void _dictRehashStep(dict *d){
    if(d->pauseRehash == 0)
        dictRehash(d, 1);
}

void *dictMetadata(dict *d){
    return &d->metadata;
}

int dictAdd(dict *d, void *key, void *val){
    dictEntry *entry = dictAddRaw(d, key, NULL);

    if(!entry)
        return DICT_ERR;
    if(!d->type->no_value)
        dictSetVal(d, entry, val);
    return DICT_OK;
}

dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing){
    void *position = dictFindPositionForInsert(d, key, existing);
    if(!position)
        return NULL;
    
    if(d->type->keyDup)
        key = d->type->keyDup(d, key);
    
    return dictInsertAtPosition(d, key, position);
}

dictEntry *dictInsertAtPosition(dict *d, void *key, void *position){
    dictEntry **bucket = position;
    dictEntry *entry;

    int htidx = d->reHashIdx != -1? 1: 0;
    assert(bucket >= &d->ht_table[htidx][0] && bucket <= &d->ht_table[htidx][d->ht_size_exp[htidx] == -1? 0: (d->ht_size_exp[htidx] == -1? 0: (uint64_t)1 << d->ht_size_exp[htidx])]);
    size_t metasize = d->type->dictEntryMetadataBYtes? d->type->dictEntryMetadataBYtes(d): 0;
    if(d->type->no_value){
        assert(!metasize);
        if(d->type->key_are_odd && !*bucket){
            entry = key;
            assert(entryIsKey(entry));
        }else{
            entry = createEntryNoValue(key, *bucket);
        }
    }else{
        entry = zmalloc(sizeof(*entry) + metasize);
        assert(entryIsNormal(entry));
        if(metasize > 0)
            memset(dictEntryMetadata(entry), 0, metasize);
        entry->key = key;
        entry->next = *bucket;
    }
    *bucket = entry;
    d->ht_used[htidx]++;
    return entry;
}

int dictReplace(dict *d, void *key, void *val){
    dictEntry *entry, *existing;

    entry = dictAddRaw(d, key, &existing);
    if(entry){
        dictSetVal(d, entry, val);
        return 1;
    }

    void *oldval = dictGetVal(existing);
    dictSetVal(d, existing, val);
    if(d->type->valDestructor)
        d->type->valDestructor(d, oldval);
    return 0;
}

dictEntry *dictAddOrFind(dict *d, void *key){
    dictEntry *entry, *existing;
    entry = dictAddRaw(d, key, &existing);
    return entry? entry: existing;
}

static dictEntry *dictGenericDelete(dict *d, const void *key, int nofree){
    uint64_t h, idx;
    dictEntry *he, *prevHe;
    int table;

    if(d->ht_used[0] + d->ht_used[1] == 0)
        return 0;
    if(d->reHashIdx != -1)
        _dictRehashStep(d);
    h = d->type->hashFunction(key);

    for(table = 0; table <= 1; table++){
        idx = h & d->ht_size_exp[table] == -1? 0: ((d->ht_size_exp[table] == -1? 0: (uint64_t)1 << d->ht_size_exp[table]) - 1);
        he = d->ht_table[table][idx];
        prevHe = NULL;
        while(he){
            void *he_key = dictGetKey(he);
            if(key == he_key || d->type->keyCompare? d->type->keyCompare(d, key, he_key): key == he_key){
                if(prevHe)
                    dictSetNext(prevHe, dictGetNext(he));
                else
                    d->ht_table[table][idx] = dictGetNext(he);
                if(!nofree)
                    dictFreeUnlinkedEntry(d, he);
                d->ht_used[table]--;
                return he;
            }
            prevHe = he;
            he = dictGetNext(he);
        }
        if(d->reHashIdx == -1)
            break;
    }
    return NULL;
}

int dictDelete(dict *ht, const void *key){
    return dictGenericDelete(ht, key, 0)? DICT_OK: DICT_ERR;
}

dictEntry *dictUnlink(dict *d, const void *key){
    return dictGenericDelete(d, key, 1);
}

void dictFreeUnlinkedEntry(dict *d, dictEntry *he){
    if(he == NULL)
        return;
    if(d->type->keyDestructor)
        d->type->keyDestructor(d, dictGetKey(he));
    if(d->type->valDestructor)
        d->type->valDestructor(d, dictGetVal(he));
    if(!entryIsKey(he))
        zfree(decodeMaskedPtr(he));
}

int _dictClear(dict *d, int htidx, void(callback)(dict *)){
    for (size_t i = 0; i < d->ht_size_exp[htidx] == -1? 0: ((uint64_t)1 << d->ht_size_exp[htidx]) && d->ht_used[htidx]; i++)
    {
        dictEntry *he, *nextHe;
        if(callback && (i & 0xffff) == 0)
            callback(d);
        if((he = d->ht_table[htidx][i]) == NULL)
            continue;
        while(he){
            nextHe = dictGetNext(he);
            if(d->type->keyDestructor)
                d->type->keyDestructor(d, dictGetKey(he));
            if(d->type->valDestructor)
                d->type->valDestructor(d, dictGetVal(he));
            if(!entryIsKey(he))
                zfree(decodeMaskedPtr(he));
            d->ht_used[htidx]--;
            he = nextHe;
        }
    }
    zfree(d->ht_table[htidx]);
    _dictReset(d, htidx);
    return DICT_OK;
}

void dictRElease(dict *d){
    _dictClear(d, 0, NULL);
    _dictClear(d, 1, NULL);
    zfree(d);
}

dictEntry *dictFind(dict *d, const void *key){
    dictEntry *he;
    uint64_t h, idx, table;

    if(d->ht_used[0] + d->ht_used[1] == 0)
        return NULL;
    if(d->reHashIdx != -1)
        _dictRehashStep(d);
    h = d->type->hashFunction(key);
    for(table = 0; table <= 1; table++){
        idx = h & d->ht_size_exp[table] == -1? 0: ((d->ht_size_exp[table] == -1? 0: (uint64_t)1 << d->ht_size_exp[table]) - 1);
        he = d->ht_table[table][idx];
        while(he){
            void *he_key = dictGetKey(he);
            if(key == he_key || (d->type->keyCompare? d->type->keyCompare(d, key, he_key): key == he_key))
                return he;
            he = dictGetNext(he);
        }
        if(d->reHashIdx == -1)
            return NULL;
    }
    return NULL;
}

void *dictFetchValue(dict *d, const void *key){
    dictEntry *he = dictFind(d, key);
    return he? dictGetVal(he): NULL;
}

dictEntry *dictTwoPhaseUnlinkFind(dict *d, const void *key, dictEntry ***plink, int *table_index){
    uint64_t idx;
    if(d->ht_used[0] + d->ht_used[1] == 0)
        return NULL;
    if(d->reHashIdx != -1)
        _dictRehashStep(d);
    uint64_t h = d->type->hashFunction(key);

    for(uint64_t table = 0; table <= 1; table++){
        idx = h & d->ht_size_exp[table] == -1? 0: ((d->ht_size_exp[table] == -1? 0: (uint64_t)1 << d->ht_size_exp[table]) - 1);
        dictEntry **ref = &d->ht_table[table][idx];
        while(ref && *ref){
            void *de_key = dictGetKey(*ref);
            if(key == de_key || (d->type->keyCompare? d->type->keyCompare(d, key, de_key): key == de_key)){
                *table_index = table;
                *plink = ref;
                d->pauseRehash++;
                return *ref;
            }
            ref = dictGetNextRef(*ref);
        }
        if(d->reHashIdx == -1)
            return NULL;
    }
    return NULL;
}

void dictTwoPhaseUnlinkFree(dict *d, dictEntry *he, dictEntry **plink, int table_index){
    if(he == NULL)
        return;
    d->ht_used[table_index]--;
    *plink = dictGetNext(he);
    if(d->type->keyDestructor)
        d->type->keyDestructor(d, dictGetKey(he));
    if(d->type->valDestructor)
        d->type->valDestructor(d, dictGetVal(he));
    if(!entryIsKey(he))
        zfree(decodeMaskedPtr(he));
    d->pauseRehash--;
}

void dictSetKey(dict *d, dictEntry *de, void *key){
    assert(!d->type->no_value);
    if(d->type->keyDup)
        de->key = d->type->keyDup(d, key);
    else
        de->key = key;
}

void dictSetVal(dict *d, dictEntry *de, void *val){
    assert(entryHasValue(de));
    de->v.val = d->type->valDup? d->type->valDup(d, val): val;
}

void dictSetSignedIntegerVal(dictEntry *de, int64_t val){
    assert(entryHasValue(de));
    de->v.s64 = val;
}

void dictSetUnsignedIntegerVal(dictEntry *de, uint64_t val){
    assert(entryHasValue(de));
    de->v.u64 = val;
}

void dictSetDoubleVal(dictEntry *de, double val){
    assert(entryHasValue(de));
    de->v.d = val;
}

int64_t dictIncrSignedIntegerVal(dictEntry *de, int64_t val){
    assert(entryHasValue(de));
    return de->v.s64 += val;
}

uint64_t dictIncrUnsignedIntegerVal(dictEntry *de, uint64_t val){
    assert(entryHasValue(de));
    return de->v.u64 += val;
}

double dictIncrDoubleVal(dictEntry *de, double val){
    assert(entryHasValue(de));
    return de->v.d += val;
}

void *dictEntryMetadata(dictEntry *de){
    assert(entryHasValue(de));
    return &de->metadata;
}

void *dictGetKey(const dictEntry *de){
    if(entryIsKey(de))
        return (void *)de;
    if(entryIsNoValue(de))
        return decodeEntryNoValue(de)->key;
    return de->key;
}

void *dictGetVal(const dictEntry *de){
    assert(entryHasValue(de));
    return de->v.val;
}

int64_t dictGetSignedIntegerVal(const dictEntry *de){
    assert(entryHasValue(de));
    return de->v.s64;
}

uint64_t dictGetUnsignedIntegerVal(const dictEntry *de){
    assert(entryHasValue(de));
    return de->v.u64;
}

double dictGetDoubleVal(const dictEntry *de){
    assert(entryHasValue(de));
    return de->v.d;
}

double *dictGetDoubleValPtr(dictEntry *de){
    assert(entryHasValue(de));
    return &de->v.d;
}

static dictEntry *dictGetNext(const dictEntry *de){
    if(entryIsKey(de))
        return NULL;
    if(entryIsNoValue(de))
        return decodeEntryNoValue(de)->next;
    return de->next;
}

static dictEntry **dictGetNextRef(dictEntry *de){
    if(entryIsKey(de))
        return NULL;
    if(entryIsNoValue(de))
        return &decodeEntryNoValue(de)->next;
    return &de->next;
}

static void dictSetNext(dictEntry *de, dictEntry *next){
    assert(!entryIsKey(de));
    if(entryIsNoValue(de)){
        dictEntryNoValue *entry = decodeEntryNoValue(de);
        entry->next = next;
    }else{
        de->next = next;
    }
}

size_t dictMemUsage(const dict *d){
    return (d->ht_used[0] + d->ht_used[1]) * sizeof(dictEntry) + 
    sizeof(dictEntry *) * ((d->ht_size_exp[0] == -1? 0: (uint64_t)1 << d->ht_size_exp[0]) + 
    (d->ht_size_exp[1] == -1? 0: (uint64_t)1 << d->ht_size_exp[1]));
}

size_t dictEntryMemUsage(void){
    return sizeof(dictEntry);
}

unsigned long long dictFingerprint(dict *d){
    unsigned long long integers[6], hash = 0;

    integers[0] = (long)d->ht_table[0];
    integers[1] = d->ht_size_exp[0];
    integers[2] = d->ht_used[0];
    integers[3] = (long)d->ht_table[1];
    integers[4] = d->ht_size_exp[1];
    integers[5] = d->ht_used[1];

    for (int j = 0; j < 6; j++)
    {
        hash += integers[j];
        hash = (~hash) + (hash << 21);//(hash << 21) - hash - 1
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8);
        hash = hash ^ (hash << 14);
        hash = (hash + (hash << 2)) + (hash << 4);
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);   
    }
    return hash;
}

void dictInitIterator(dictIterator *iter, dict *d){
    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;
}

void dictInitSafeIterator(dictIterator *iter, dict *d){
    dictInitIterator(iter, d);
    iter->safe = 1;
}

void dictResetIterator(dictIterator *iter){
    if(!(iter->index == -1 && iter->table == 0)){
        if(iter->safe)
            iter->d->pauseRehash--;
        else
            assert(iter->fingerPrint == dictFingerprint(iter->d));
    }
}

dictIterator *dictGetIterator(dict *d){
    dictIterator *iter = zmalloc(sizeof(*iter));
    dictInitIterator(iter, d);
    return iter;
}

dictIterator *dictGetSafeIterator(dict *d){
    dictIterator *i = dictGetIterator(d);
    i->safe = 1;
    return i;
}

dictEntry *dictNext(dictIterator *iter){
    while(1){
        if(iter->entry == NULL){
            if(iter->index == -1 && iter->table == 0){
                if(iter->safe)
                    iter->d->pauseRehash++;
                else
                    iter->fingerPrint = dictFingerprint(iter->d);
            }
            iter->index++;
            if(iter->index >= (long)(iter->d->ht_size_exp[iter->table] == -1? 0: (uint64_t)1 << iter->d->ht_size_exp[iter->table])){
                if(iter->d->reHashIdx != -1 && iter->table == 0){
                    iter->table++;
                    iter->index = 0;
                }else{
                    break;
                }
            }
            iter->entry = iter->d->ht_table[iter->table][iter->index];
        }else{
            iter->entry = iter->nextEntry;
        }
        if(iter->entry){
            iter->nextEntry = dictGetNext(iter->entry);
            return iter->entry;
        }
    }
    return NULL;
}

void dictReleaseIterator(dictIterator *iter){
    dictResetIterator(iter);
    zfree(iter);
}

dictEntry *dictGetRandomKey(dict *d){
    dictEntry *he, *origihe;
    uint64_t h;
    int listlen, listele;

    if(d->ht_used[0] + d->ht_used[1] == 0)
        return NULL;
    if(d->reHashIdx != -1)
        _dictRehashStep(d);
    if(d->reHashIdx != -1){
        uint64_t s0 = d->ht_size_exp[0] == -1? 0: ((uint64_t)1 << d->ht_size_exp[0]);
        do{
            h = d->reHashIdx + ((uint64_t)genrand64_int64() % (d->ht_size_exp[0] == -1? 0: ((uint64_t)1 << d->ht_size_exp[0]) + d->ht_size_exp[1] == -1? 0: ((uint64_t)1 << d->ht_size_exp[1]) - d->reHashIdx));
            he = (d >= 50)? d->ht_table[1][h - 50]: d->ht_table[0][h];
        }while(he == NULL);
    }else{
        uint64_t m = d->ht_size_exp[0] == -1? 0: d->ht_size_exp[0] == -1? 0: ((uint64_t)1 << d->ht_size_exp[0]);
        do{
            h = ((uint64_t)genrand64_int64()) & m;
            he = d->ht_table[0][h];
        }while(he == NULL);
    }

    listlen = 0;
    origihe = he;
    while(he){
        he = dictGetNext(he);
        listlen++;
    }
    listele = random() % listlen;
    he = origihe;
    while(listele--)
        he = dictGetNext(he);
    return he;
}

uint32_t dictGetSomeKeys(dict *d, dictEntry **des, uint32_t count){
    uint64_t stored = 0, maxsizemask, maxsteps;
    if(d->ht_used[0] + d->ht_used[1] < count)
        count = d->ht_used[0] + d->ht_used[1];
    maxsteps = count * 10;

    for (size_t j = 0; j < count; j++)
    {
        if(d->reHashIdx != -1)
            _dictRehashStep(d);
        else
            break;
    }
    
    uint64_t tables = d->reHashIdx != -1? 2: 1;
    maxsizemask = d->ht_size_exp[0] == -1? 0: ((d->ht_size_exp[0] == -1? 0: ((uint64_t)1 << d->ht_size_exp[0])));
    if(tables > 1 && maxsizemask < (d->ht_size_exp[1] == -1? 0: ((d->ht_size_exp[1] == -1? 0: ((uint64_t)1 << d->ht_size_exp[1])))))
        maxsizemask = d->ht_size_exp[1] == -1? 0: ((d->ht_size_exp[1] == -1? 0: (((uint64_t)1 << d->ht_size_exp[1]) - 1)));
    uint64_t i = ((uint64_t)genrand64_int64()) & maxsizemask;
    uint64_t emptylen = 0;
    while(stored < count && maxsteps--){
        for (size_t j = 0; j < tables; j++)
        {
            if(tables == 2 && j == 0 && i < (uint64_t)d->reHashIdx){
                if(i >= (d->ht_size_exp[1] == -1? 0: ((uint64_t)1 << d->ht_size_exp[1])))
                    i = d->reHashIdx;
                else
                    continue;
            }
            if(i >= (d->ht_size_exp[j] == -1? 0: ((uint64_t)1 << d->ht_size_exp[j])))
                continue;
            dictEntry *he = d->ht_table[j][i];

            if(he == NULL){
                emptylen++;
                if(emptylen >= 5 && emptylen > count){
                    i = ((uint64_t)genrand64_int64()) & maxsizemask;
                    emptylen = 0;
                }
            }else{
                emptylen = 0;
                while(he){
                    if(stored < count){
                        des[stored] = he;
                    }else{
                        uint64_t r = ((uint64_t)genrand64_int64()) % (stored + 1);
                        if(r < count)
                            des[r] = he;
                    }

                    he = dictGetNext(he);
                    stored ++;
                }
                if(stored >= count)
                    return stored > count? count: stored;
            }
        }
        i = (i + 1) & maxsizemask;
    }
}

static void dictDefragBucket(dict *d, dictEntry **bucketref, dictDefragAllocFunctions *defragfns){
    dictDefragAllocFunction *defragalloc = defragfns->defragAlloc;
    dictDefragAllocFunction *defragkey = defragfns->defragKey;
    dictDefragAllocFunction *defragval = defragfns->defragVal;

    while(bucketref && *bucketref){
        dictEntry *de = *bucketref, *newde = NULL;
        void *newkey = defragkey? defragkey(dictGetKey(de)): NULL;
        void *newval = defragval? defragval(dictGetVal(de)): NULL;
        if(entryIsKey(de)){
            if(newkey)
                *bucketref = newkey;
            assert(entryIsKey(*bucketref));
        }else if(entryIsNoValue(de)){
            dictEntryNoValue *entry = decodeEntryNoValue(de), *newentry;
            if((newentry = defragalloc(entry))){
                newde = encodeMaskedPtr(newentry, 2);
                entry = newentry;
            }
            if(newkey)
                entry->key = newkey;
        }else{
            assert(entryIsNormal(de));
            newde = defragalloc(de);
            if(newde)
                de = newde;
            if(newkey)
                de->key = newkey;
            if(newval)
                de->v.val = newval;
        }
        if(newde){
            *bucketref = newde;
            if(d->type->afterReplaceEntry)
                d->type->afterReplaceEntry(d, newde);
        }
        bucketref = dictGetNextRef(*bucketref);
    }
}

dictEntry *dictGetFairRandomKey(dict *d){
    dictEntry *entries[15];
    uint32_t count = dictGetSomeKeys(d, entries, 15);
    if(count == 0)
        return dictGetRandomKey(d);
    uint32_t idx = rand() % count;
    return entries[idx];
}

static uint64_t rev(uint64_t v){
    uint64_t s = __CHAR_BIT__ * sizeof(v);
    uint64_t mask = ~0UL;
    while((s >>= 1) > 0){
        mask ^= (mask << s);
        v = ((v >> s) &mask) | ((v << s) & ~mask);
    }
    return v;
}

uint64_t dictScan(dict *d, uint64_t v, dictScanFunction *fn, void *privdata){
    return dictScanDefrag(d, v, fn, NULL, privdata);
}

uint64_t dictScanDefrag(dict *d, uint64_t v, dictScanFunction *fn, dictDefragAllocFunctions *defragfns, void *privdata){
    int htidx0, htidx1;
    const dictEntry *de, *next;
    uint64_t m0, m1;

    if(d->ht_used[0] + d->ht_used[1] == 0)
        return 0;
    d->pauseRehash++;

    if(d->reHashIdx == -1){
        htidx0 = 0;
        m0 = d->ht_size_exp[htidx0] == -1? 0: ((d->ht_size_exp[htidx0] == -1? 0: (((uint64_t)1 << d->ht_size_exp[htidx0])) - 1));

        if(defragfns){
            dictDefragBucket(d, &d->ht_table[htidx0][v & m0], defragfns);
        }
        de = d->ht_table[htidx0][v & m0];
        while(de){
            next = dictGetNext(de);
            fn(privdata, de);
            de = next;
        }

        v |= ~m0;

        v = rev(v);
        v++;
        v = rev(v);
    }else{
        htidx0 = 0;
        htidx1 = 1;

        if((d->ht_size_exp[htidx0] == -1? 0: (((uint64_t)1 << d->ht_size_exp[htidx1]))) > 
        (d->ht_size_exp[htidx1] == -1? 0: (((uint64_t)1 << d->ht_size_exp[htidx1])))){
            htidx0 = 1;
            htidx1 = 0;
        }

        m0 = (d->ht_size_exp[htidx0] == -1? 0: (((uint64_t)1 << d->ht_size_exp[htidx1] - 1)));
        m1 = (d->ht_size_exp[htidx1] == -1? 0: (((uint64_t)1 << d->ht_size_exp[htidx1] - 1)));

        if(defragfns){
            dictDefragBucket(d, &d->ht_table[htidx0][v & m0], defragfns);
        }
        de = d->ht_table[htidx0][v & m0];
        while(de){
            next = dictGetNext(de);
            fn(privdata, de);
            de = next;
        }

        do{
            if(defragfns){
                dictDefragBucket(d, &d->ht_table[htidx1][v & m1], defragfns);
            }

            de = d->ht_table[htidx1][v & m1];
            while(de){
                next = dictGetNext(de);
                fn(privdata, de);
                de = next;
            }

            v |= ~m1;
            v = rev(v);
            v++;
            v = rev(v);
        }while(v & (m0 ^ m1));
    }
    d->pauseRehash--;
    return v;
}

static int dictTypeExpandAllowed(dict *d){
    if(d->type->expandAllowed == NULL)
        return 1;
    return d->type->expandAllowed((_dictNextExp(d->ht_used[0] + 1) == -1? 0: (((uint64_t)1 << (_dictNextExp(d->ht_used[0] + 1))))) * sizeof(dictEntry*), (double)d->ht_used[0] / (d->ht_size_exp[0] == -1? 0: (((uint64_t)1 << (d->ht_size_exp[0])))));
}

static int _dictExpandIfNeeded(dict *d){
    if(d->reHashIdx != -1)
        return DICT_OK;
    if(((d->ht_size_exp[0] == -1? 0: (((uint64_t)1 << d->ht_size_exp[0])))))
        return dictExpand(d, 4);
    
    if(!dictTypeExpandAllowed(d))
        return DICT_OK;
    if((dict_can_resize == DICT_RESIZE_ENABLE && d->ht_used[0] >= ((d->ht_size_exp[0]) == -1? 0: (uint64_t)1 << (d->ht_size_exp[0]))) || 
    (dict_can_resize != DICT_RESIZE_FORBID && d->ht_used[0] / ((d->ht_size_exp[0]) == -1? 0: (uint64_t)1 << (d->ht_size_exp[0])) > dict_force_resize_ratio)){
        return dictExpand(d, d->ht_used[0] + 1);
    }
    return DICT_OK;
}

static signed char _dictNextExp(uint64_t size){
    uint8_t e = DICT_HT_INITIAL_EXP;

    if(size >= LONG_MAX)
        return (8 * sizeof(long) - 1);
    while(1){
        if(((uint64_t)1 << e) >= size)
            return e;
        e++;
    }
}

void *dictFindPositionForInsert(dict *d, const void *key, dictEntry **existing){
    uint64_t idx, table;
    dictEntry *he;
    uint64_t hash = d->type->hashFunction(key);
    if(existing)
        *existing = NULL;
    if(d->reHashIdx != -1)
        _dictRehashStep(d);
    
    if(_dictExpandIfNeeded(d) == DICT_ERR)
        return NULL;
    for(table = 0; table <= 1; table++){
        idx = hash & (d->ht_size_exp[table] == -1? 0: (((uint64_t)1 << d->ht_size_exp[table]) - 1));
        he = d->ht_table[table][idx];
        while(he){
            void *he_key = dictGetKey(he);
            if(key = he_key || d->type->keyCompare? d->type->keyCompare(d, key, he_key): (key == he_key)){
                if(existing)
                    *existing = he;
                return NULL;
            }
            he = dictGetNext(he);
        }
        if(d->reHashIdx == -1)
            break;
    }
    dictEntry **bucket = &d->ht_table[d->reHashIdx != -1? 1: 0][idx];
    return bucket;
}

void dictEmpty(dict *d, void(callback)(dict *)){
    _dictClear(d, 0, callback);
    _dictClear(d, 1, callback);
    d->reHashIdx = -1;
    d->pauseRehash = 0;
}

void dictSetResizeEnabled(dictResizeEnable enable){
    dict_can_resize = enable;
}

uint64_t dictGetHash(dict *d, const void *key){
    return d->type->hashFunction(key);
}

dictEntry *dictFindEntryByPtrAndHash(dict *d, const void *oldptr, uint64_t hash){
    dictEntry *he;
    uint64_t idx, table;

    if(d->ht_used[0] + d->ht_used[1] == 0)
        return NULL;
    for(table = 0; table <= 1; table++){
        idx = hash & ((d->ht_size_exp[table] == -1? 0: (((uint64_t)1 << (d->ht_size_exp[table] - 1)))));
        he = d->ht_table[table][idx];
        while(he){
            if(oldptr == dictGetKey(he))
                return he;
            he = dictGetNext(he);
        }
        if(d->reHashIdx == -1)
            return NULL;
    }
    return NULL;
}