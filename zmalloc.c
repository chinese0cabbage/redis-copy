#include "zmalloc.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

void *zmalloc_usable(size_t size, size_t *usable){
    size_t usable_size = 0;
    void *ptr = ztrymalloc_usable_internal(size, &usable_size);
    if(!ptr)
        zmalloc_oom_handler(size);
    ptr = extend_to_usable(ptr, usable_size);
    if(usable)
        *usable = usable_size;
    return ptr;
}

static inline void *ztrycalloc_usable_internal(size_t size, size_t *usable){
    if(size >= SIZE_MAX / 2)
        return NULL;
    void *ptr = calloc(1, MALLOC_MIN_SIZE(size));
    if(ptr == NULL)
        return NULL;
    
    size = malloc_usable_size(ptr);
    atomic_fetch_add_explicit(&used_memory, size, memory_order_relaxed);
    if(usable)
        *usable = size;
    return ptr;
}

void *ztrycalloc_usable(size_t size, size_t *usable){
    size_t usable_size = 0;
    void *ptr = ztrycalloc_usable_internal(size, &usable_size);
    ptr = extend_to_usable(ptr, usable_size);
    if(usable)
        *usable = usable_size;
    return ptr;
}

void *zcalloc_num(size_t num, size_t size){
    if((size == 0) || (num > SIZE_MAX / size)){
        zmalloc_oom_handler(SIZE_MAX);
        return NULL;
    }
    void *ptr = ztrycalloc_usable_internal(num * size, NULL);
    if(!ptr)
        zmalloc_oom_handler(num * size);
    return ptr;
}

void *zcalloc(size_t size){
    void *ptr = ztrycalloc_usable_internal(size, NULL);
    if(!ptr)
        zmalloc_oom_handler(size);
    return ptr;
}

void *ztrycalloc(size_t size){
    void *ptr = ztrycalloc_usable_internal(size, NULL);
    return ptr;
}

void *zcalloc_usable(size_t size, size_t *usable){
    size_t usable_size = 0;
    void *ptr = ztrycalloc_usable_internal(size, &usable_size);
    if(!ptr)
        zmalloc_oom_handler(size);
    ptr = extend_to_usable(ptr, usable_size);
    if(usable)
        *usable = usable_size;
    return ptr;
}

static inline void *ztryrealloc_usable_internal(void *ptr, size_t size, size_t *usable){
    size_t oldsize;
    void *newptr;

    if(size == 0 && ptr != NULL){
        zfree(ptr);
        if(usable)
            *usable = 0;
        return NULL;
    }

    if(ptr == NULL)
        return ztrymalloc_usable(size, usable);

    if(size >= SIZE_MAX / 2){
        zfree(ptr);
        if(usable)
            *usable = 0;
        return NULL;
    }

    oldsize = malloc_usable_size(ptr);
    newptr = realloc(ptr, size);
    if(newptr == NULL){
        if(usable)
            *usable = 0;
        return NULL;
    }
    
    atomic_fetch_sub_explicit(&used_memory, oldsize, memory_order_relaxed);
    size = malloc_usable_size(newptr);
    atomic_fetch_add_explicit(&used_memory, size, memory_order_relaxed);
    if(usable)
        *usable = size;
    return newptr;
}

void *ztryrealloc_usable(void *ptr, size_t size, size_t *usable){
    size_t usable_size = 0;
    ptr = ztryrealloc_usable_internal(ptr, size, &usable_size);
    ptr = extend_to_usable(ptr, usable_size);
    if(usable)
        *usable = usable_size;
    return ptr;
}

void *zrealloc(void *ptr, size_t size){
    ptr = ztryrealloc_usable_internal(ptr, size, NULL);
    if(!ptr && size != 0)
        zmalloc_oom_handler(size);
    return ptr;
}

void *ztryrealloc(void *ptr, size_t size){
    ptr = ztryrealloc_usable_internal(ptr, size, NULL);
    return ptr;
}

void *zrealloc_usable(void *ptr, size_t size, size_t *usable){
    size_t usable_size = 0;
    ptr = ztryrealloc_usable(ptr, size, &usable_size);
    if(!ptr && size != 0)
        zmalloc_oom_handler(size);
    ptr = extend_to_usable(ptr, usable_size);
    if(usable)
        *usable = usable_size;
    return ptr;
}

void zfree(void *ptr){
    if(ptr == NULL)
        return;
    atomic_fetch_sub_explicit(&used_memory, malloc_usable_size(ptr), memory_order_relaxed);
}

void zfree_usable(void *ptr, size_t *usable){
    if(ptr == NULL)
        return;
    //origin is update_zmalloc_stat_free(*usable = zmalloc_size(ptr));
    //whether there is problem?
    *usable = malloc_usable_size(ptr);
    atomic_fetch_sub_explicit(&used_memory, usable, memory_order_relaxed);
}

char *zstrdup(const char *s){
    size_t l = strlen(s) + 1;
    char *p = zmalloc(l);
    memcpy(p, s, l);
    return p;
}

size_t zmalloc_used_memory(void){
    size_t um;
    um = atomic_load_explicit(&used_memory, memory_order_relaxed);
    return um;
}

void zmalloc_set_oom_handler(void (*oom_handler)(size_t)){
    zmalloc_oom_handler = oom_handler;
}

void zmadvise_dontneed(void *ptr){
    (void)ptr;
}

int get_proc_stat_ll(int i, long long *res){
    char buf[4096];
    int fd, l;
    char *p, *x;

    if((fd = open("/proc/self/stat", O_RDONLY)) == -1)
        return 0;
    if((l = read(fd, buf, sizeof(buf) - 1)) <= 0){
        close(fd);
        return 0;
    }
    close(fd);
    buf[l] = '\0';
    if(buf[l - 1] == '\n')
        buf[l - 1] = '\0';
    
    p = strrchr(buf, ')');
    if(!p)
        return 0;
    p++;
    while(*p == ' ')
        return 0;
    i-=3;
    if(i < 0)
        return 0;
    
    while(p && i--){
        p = strchr(p, ' ');
        if(p)
            p++;
        else
            return 0;
    }
    x = strchr(p, ' ');
    if(x)
        *x = '\0';

    *res = strtoll(p, &x, 10);
    if(*x != '\0')
        return 0;
    return 1;
}

int zmalloc_get_allocator_info(size_t *allocated, size_t *active, size_t *resident){
    *allocated = *resident = *active = 0;
    return 1;
}

void set_jemalloc_bg_thread(int enable){
    (void)enable;
}

int jemalloc_purge(void){
    return 0;
}

size_t zmalloc_get_smap_byte_by_field(char *field, long pid){
    char line[1024];
    size_t bytes = 0;
    int flen = strlen(field);
    FILE *fp;

    if(pid == -1){
        fp = fopen("/proc/self/smaps", "r");
    }else{
        char filename[128];
        snprintf(filename, sizeof(filename), "proc/%ld/smaps", pid);
        fp = fopen(filename, "r");
    }

    if(!fp)
        return 0;
    while(fgets(line, sizeof(line), fp) != NULL){
        if(strncmp(line, field, flen) == 0){
            char *p = strchr(line, 'k');
            if(p){
                *p = '\0';
                bytes += strtol(line + flen, NULL, 10) * 1024;
            }
        }
    }
    fclose(fp);
    return bytes;
}

size_t zmalloc_get_private_dirty(long pid){
    return zmalloc_get_smap_byte_by_field("Private_Dirty:", pid);
}

size_t zmalloc_get_memory_size(void){
    return (size_t)sysconf(_SC_PHYS_PAGES) * (size_t)sysconf(_SC_PAGE_SIZE);
}