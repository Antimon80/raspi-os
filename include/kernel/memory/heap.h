#ifndef KERNEL_MEMORY_HEAP_H
#define KERNEL_MEMORY_HEAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uintptr_t start;
    uintptr_t end;
    size_t total_bytes;
    size_t used_bytes;
    size_t free_bytes;
    size_t block_count;
    size_t used_block_count;
    size_t free_block_count;
} heap_stats_t;

void heap_init(void);
void *kmalloc(size_t size);
void *kmalloc_zero(size_t size);
void kfree(void *ptr);

void heap_dump(void);
void heap_get_stats(heap_stats_t *stats);

#endif