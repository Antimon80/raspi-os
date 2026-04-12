#ifndef KERNEL_MEMORY_HEAP_H
#define KERNEL_MEMORY_HEAP_H

#include <stddef.h>
#include <stdint.h>

void heap_init(void);
void *kmalloc(size_t size);
void *kmalloc_zero(size_t size);
void kfree(void *ptr);
void heap_dump(void);
void heap_stats(void);

#endif