#include "zmalloc.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>

void zlibc_free(void *ptr){
    free(ptr);
}

#define MALLOC_MIN_SIZE(x) ((x) > 0 ? (x) : sizeof(long))

static _Atomic size_t used_memory = 0;

static void zmalloc_default_oom(size_t size){
    fprintf(stderr, "zmalloc: Out of memory trying to alloc %zu bytes\n", size);
    fflush(stderr);
    abort();
}

static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

void *extend_to_usable(void *ptr, size_t size){
    (void)size;
    return ptr;
}

//alloc size byte memory, but the real usable memory is bigger than size
//*usable save the real alloc memory, used_memory increase *usable
static inline void *ztrymalloc_usable_internal(size_t size, size_t *usable){
    if(size >= SIZE_MAX/2)
        return NULL;
    void *ptr = malloc(MALLOC_MIN_SIZE(size));
    if(!ptr)
        return NULL;
    size = malloc_usable_size(ptr);
    atomic_fetch_add_explicit(&used_memory, size, memory_order_relaxed);
    if(usable)
        *usable = size;
    return ptr;
}

void *ztrymalloc_usable(size_t size, size_t *usable){
    size_t usable_size = 0;
    void *ptr = ztrymalloc_usable_internal(size, &usable_size);
    ptr = extend_to_usable(ptr, usable_size);
    if(usable)
        *usable = usable_size;
    return ptr;
}

void *zmalloc(size_t size){
    void *ptr = ztrymalloc_usable_internal(size, NULL);
    if(!ptr)
        zmalloc_oom_handler(size);
    return ptr;
}

void *ztrymalloc(size_t size){
    void *ptr = ztrymalloc_usable_internal(size, NULL);
    return ptr;
}