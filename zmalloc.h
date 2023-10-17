#pragma once
#include <malloc.h>

#define zmalloc_size(p) malloc_usable_size(p)

//those __attribute__ is used to avoid "-Wstringop-overread" warning, 
//since gcc-12 and with LTO enabled this bug may be removed once
__attribute__((malloc, alloc_size(1), noinline))
void *zmalloc(size_t size);

__attribute__((malloc, alloc_size(1), noinline))
void *zcalloc(size_t size);

__attribute__((malloc, alloc_size(1, 2), noinline))
void *zcalloc_num(size_t num, size_t size);

__attribute__((alloc_size(2), noline))
void *zrealloc(void *ptr, size_t size);

__attribute__((malloc, alloc_size(1), noinline))
void *ztrymalloc(size_t size);

__attribute__((malloc, alloc_size(1), noinline))
void *ztrycalloc(size_t size);

__attribute__((alloc_size(2), noinline))
void *ztryrealloc(void *ptr, size_t size);

void zfree(void *ptr);

void *zmalloc_usable(size_t size, size_t *usable);

void *zcalloc_usable(size_t size, size_t *usable);

void *zrealloc_usable(void *ptr, size_t size, size_t *usable);

void *ztrymalloc_usable(size_t size, size_t *usable);

void *ztrycalloc_usable(size_t size, size_t *usable);

void *ztryrealloc_usable(void *ptr, size_t size, size_t *usable);

void zfree_usable(void *ptr, size_t *usable);

__attribute__((malloc))
char *zstrdup(const char *s);

size_t zmalloc_used_memory(void);

void zmalloc_set_oom_handler(void(*oom_handler)(size_t));

size_t zmalloc_get_rss(void);

int zmalloc_get_allocator_info(size_t *allocated, size_t *active, size_t *resident);

//multy thread?
void set_jemalloc_bg_thread(int enable);

int jemalloc_purge(void);

size_t zmalloc_get_private_dirty(long pid);

size_t zmalloc_get_smap_byte_by_field(char *field, long pid);

size_t zmalloc_get_memory_size(void);

void zlibc_free(void *ptr);

void zmadvise_dontneed(void *ptr);

#define zmalloc_usable_size(p) zmalloc_size(p)

__attribute__((alloc_size(2), noinline))
void *extend_to_usable(void *ptr, size_t size);

int get_proc_stat_ll(int i, long long *res);