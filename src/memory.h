#ifndef MEMORY_H
#define MEMORY_H

#include <stdlib.h>
#include <stdio.h>

static void *xmalloc(size_t size)
{
    void *p = malloc(size);
    if (!p) {
        perror("malloc");
        exit(42);
    }
    return p;
}

static void *xrealloc(void *p, size_t size)
{
    p = realloc(p, size);
    if (!p) {
        perror("realloc");
        exit(42);
    }
    return p;
}

typedef struct
{
    size_t size;
    size_t cap;
} array_t;

#define array_header(a) \
    ((a) ? (array_t*)(void*)((char*)(a)-sizeof(array_t)) : NULL)
#define array_size(a) \
    ((a) ? array_header((a))->size : 0)
#define array_cap(a) \
    ((a) ? array_header((a))->cap : 0)
#define array_full(a) \
    (array_size((a)) == array_cap((a)))
#define array_ensure(a, size) \
    (array_cap((a)) < (size) ? (a) = array_resize((a), (size), sizeof(*(a))) : 0)
#define array_push_back(a, item) \
    (array_full((a)) ? (a) = array_resize((a), array_size((a))*2, sizeof(*(a))) : 0, \
    (a)[array_header((a))->size++] = (item))
#define array_clear(a) \
    ((a) ? array_header((a))->size = 0 : 0)
#define array_free(a) \
    if ((a)) do { free(array_header((a))); } while(0)

static inline void *array_resize(void *p, size_t size, size_t element_size)
{
    if (size == 0) size = 1;
    array_t *header = NULL;
    if (p) {
        header = xrealloc(array_header(p), sizeof *header + size*element_size);
    } else {
        header = xmalloc(sizeof *header + size*element_size);
        header->size = 0;
    }
    header->cap = size;

    return (void*)((char*)header + sizeof *header);
}

#endif
