#include "kernel/memory/heap.h"
#include "kernel/panic.h"
#include "rpi4/uart.h"

#define HEAP_SIZE (4 * 1024 * 1024)
#define HEAP_ALIGNMENT 16
#define HEAP_MAGIC 0xC0FFEE42u

/*
 * Linker symbol marking the end of the kernel image.
 * The heap starts directly after this symbol.
 */
extern char _end;

/*
 * Heap block header.
 *
 * Each allocated or free block starts with this metadata structure.
 * The user-visible pointer returned by kmalloc() points directly behind it.
 */
typedef struct heap_block
{
    size_t size;             // usable payload size in bytes
    uint32_t free;           // 1 if free, 0 if allocated
    uint32_t magic;          // sanity check for corruption
    struct heap_block *next; // next block in heap order
    struct heap_block *prev; // previous block in heap order
} heap_block_t;

static uint8_t *heap_start = 0;
static uint8_t *heap_end = 0;
static heap_block_t *heap_head = 0;
static int heap_initialized = 0;

/*
 * Align a value upwards to the next multiple of HEAP_ALIGNMENT.
 */
static uintptr_t align_up_uintptr(uintptr_t value)
{
    return (value + (HEAP_ALIGNMENT - 1)) & ~((uintptr_t)(HEAP_ALIGNMENT - 1));
}

/*
 * Convert a positive integer value to decimal ASCII.
 * Returns the number of characters written (without trailing '\0').
 */
static int utoa_dec(uint64_t value, char *buffer, int buffer_size)
{
    char temp[32];
    int temp_len = 0;
    int out_len = 0;

    if (!buffer || buffer_size <= 0)
    {
        return 0;
    }

    if (value == 0)
    {
        if (buffer_size > 1)
        {
            buffer[0] = '0';
            buffer[1] = '\0';
            return 1;
        }

        buffer[0] = '\0';
        return 0;
    }

    while (value > 0 && temp_len < (int)sizeof(temp))
    {
        temp[temp_len++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (temp_len > 0 && out_len < (buffer_size - 1))
    {
        buffer[out_len++] = temp[--temp_len];
    }

    buffer[out_len] = '\0';
    return out_len;
}

/*
 * Convert an integer value to hexadecimal ASCII.
 * Returns the number of characters written (without trailing '\0').
 */
static int utoa_hex(uint64_t value, char *buffer, int buffer_size)
{
    const char *hex = "0123456789ABCDEF";
    char temp[32];
    int temp_len = 0;
    int out_len = 0;

    if (!buffer || buffer_size <= 0)
    {
        return 0;
    }

    if (value == 0)
    {
        if (buffer_size > 1)
        {
            buffer[0] = '0';
            buffer[1] = '\0';
            return 1;
        }

        buffer[0] = '\0';
        return 0;
    }

    while (value > 0 && temp_len < (int)sizeof(temp))
    {
        temp[temp_len++] = hex[value & 0xF];
        value >>= 4;
    }

    while (temp_len > 0 && out_len < (buffer_size - 1))
    {
        buffer[out_len++] = temp[--temp_len];
    }

    buffer[out_len] = '\0';
    return out_len;
}

/*
 * Print an unsigned decimal integer to UART.
 */
static void uart_put_u64(uint64_t value)
{
    char buffer[32];
    utoa_dec(value, buffer, sizeof(buffer));
    uart_puts(buffer);
}

/*
 * Print a pointer-sized value as hexadecimal to UART.
 */
static void uart_put_hex_uintptr(uintptr_t value)
{
    char buffer[32];
    uart_puts("0x");
    utoa_hex((uint64_t)value, buffer, sizeof(buffer));
    uart_puts(buffer);
}

/*
 * Return the block header belonging to a kmalloc() pointer.
 */
static heap_block_t *ptr_to_block(void *ptr)
{
    return (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
}

/*
 * Verify that a block header looks valid.
 */
static void check_block_or_panic(heap_block_t *block, const char *context)
{
    if (!block)
    {
        kernel_panic(context);
    }

    if (block->magic != HEAP_MAGIC)
    {
        kernel_panic(context);
    }
}

/*
 * Split a free block if it is large enough to hold a second block.
 */
static void split_block(heap_block_t *block, size_t requested_size)
{
    uintptr_t block_start;
    uintptr_t new_block_addr;
    heap_block_t *new_block;
    size_t remaining_size;

    check_block_or_panic(block, "heap: split_block invalid block");

    if (!block->free)
    {
        return;
    }

    if (block->size < requested_size)
    {
        return;
    }

    if (block->size < requested_size + sizeof(heap_block_t) + HEAP_ALIGNMENT)
    {
        return;
    }

    block_start = (uintptr_t)block;
    new_block_addr = block_start + sizeof(heap_block_t) + requested_size;
    new_block_addr = align_up_uintptr(new_block_addr);

    // recompute the usable size of the first block after alignment
    requested_size = new_block_addr - block_start - sizeof(heap_block_t);

    if (block->size <= requested_size + sizeof(heap_block_t))
    {
        return;
    }

    remaining_size = block->size - requested_size - sizeof(heap_block_t);

    new_block = (heap_block_t *)new_block_addr;
    new_block->size = remaining_size;
    new_block->free = 1;
    new_block->magic = HEAP_MAGIC;
    new_block->next = block->next;
    new_block->prev = block;

    if (new_block->next)
    {
        new_block->next->prev = new_block;
    }

    block->size = requested_size;
    block->next = new_block;
}

/*
 * Merge a block with its next neighbor if both are free and adjacent.
 */
static void merge_with_next(heap_block_t *block)
{
    heap_block_t *next;
    uintptr_t expected_next;

    check_block_or_panic(block, "heap: merge invalid block");

    next = block->next;

    if (!next)
    {
        return;
    }

    check_block_or_panic(next, "heap: merge invalid next block");

    if (!block->free || !next->free)
    {
        return;
    }

    expected_next = (uintptr_t)block + sizeof(heap_block_t) + block->size;

    if ((uintptr_t)next != expected_next)
    {
        return;
    }

    block->size += sizeof(heap_block_t) + next->size;
    block->next = next->next;

    if (block->next)
    {
        block->next->prev = block;
    }
}

/*
 * Initialize the kernel heap with one large free block.
 */
void heap_init(void)
{
    uintptr_t start;
    uintptr_t end;
    size_t usable_size;

    if (heap_initialized)
    {
        return;
    }

    start = align_up_uintptr((uintptr_t)&_end);
    end = start + HEAP_SIZE;

    if (end <= start)
    {
        kernel_panic("heap: invalid heap bounds");
    }

    if ((end - start) <= sizeof(heap_block_t))
    {
        kernel_panic("heap: heap too small");
    }

    heap_start = (uint8_t *)start;
    heap_end = (uint8_t *)end;
    heap_head = (heap_block_t *)heap_start;

    usable_size = (size_t)(heap_end - heap_start - sizeof(heap_block_t));

    heap_head->size = usable_size;
    heap_head->free = 1;
    heap_head->magic = HEAP_MAGIC;
    heap_head->next = 0;
    heap_head->prev = 0;

    heap_initialized = 1;
}

/*
 * Allocate memory from the kernel heap using first-fit search.
 */
void *kmalloc(size_t size)
{
    heap_block_t *current;
    size_t aligned_size;

    if (!heap_initialized)
    {
        kernel_panic("heap: kmalloc before heap_init");
    }

    if (size == 0)
    {
        return 0;
    }

    aligned_size = (size + (HEAP_ALIGNMENT - 1)) & ~((size_t)(HEAP_ALIGNMENT - 1));

    current = heap_head;

    while (current)
    {
        check_block_or_panic(current, "heap: corrupted block in kmalloc");

        if (current->free && current->size >= aligned_size)
        {
            split_block(current, aligned_size);
            current->free = 0;
            return (void *)((uint8_t *)current + sizeof(heap_block_t));
        }

        current = current->next;
    }

    return 0;
}

void *kmalloc_zero(size_t size)
{
    uint8_t *ptr = (uint8_t *)kmalloc(size);

    if (!ptr)
    {
        return 0;
    }

    for (size_t i = 0; i < size; i++)
    {
        ptr[i] = 0;
    }

    return ptr;
}

/*
 * Free a previously allocated heap block and merge neighbors if possible.
 */
void kfree(void *ptr)
{
    heap_block_t *block;

    if (!ptr)
    {
        return;
    }

    if (!heap_initialized)
    {
        kernel_panic("heap: kfree before heap_init");
    }

    if ((uintptr_t)ptr < (uintptr_t)heap_start || (uintptr_t)ptr >= (uintptr_t)heap_end)
    {
        kernel_panic("heap: kfree pointer outside heap");
    }

    block = ptr_to_block(ptr);
    check_block_or_panic(block, "heap: invalid block in kfree");

    if (block->free)
    {
        kernel_panic("heap: double free detected");
    }

    block->free = 1;

    // merge forward first, then backward
    // backward merge may expose another forward merge opportunity
    merge_with_next(block);

    if (block->prev && block->prev->free)
    {
        merge_with_next(block->prev);
    }
}

/*
 * Print the current heap block list for debugging.
 */
void heap_dump(void)
{
    heap_block_t *current;
    int index = 0;

    if (!heap_initialized)
    {
        uart_puts("heap: not initialized\n");
        return;
    }

    uart_puts("=== HEAP DUMP ===\n");
    uart_puts("heap_start=");
    uart_put_hex_uintptr((uintptr_t)heap_start);
    uart_puts(" heap_end=");
    uart_put_hex_uintptr((uintptr_t)heap_end);
    uart_puts("\n");

    current = heap_head;

    while (current)
    {
        check_block_or_panic(current, "heap: corrupted block in dump");

        uart_puts("block ");
        uart_put_u64((uint64_t)index);
        uart_puts(": addr=");
        uart_put_hex_uintptr((uintptr_t)current);
        uart_puts(" size=");
        uart_put_u64((uint64_t)current->size);
        uart_puts(" free=");
        uart_put_u64((uint64_t)current->free);
        uart_puts("\n");

        current = current->next;
        index++;
    }

    uart_puts("=================\n");
}

void heap_stats(void)
{
    heap_block_t *current = heap_head;

    size_t free_bytes = 0;
    size_t used_bytes = 0;
    size_t blocks = 0;

    while (current)
    {
        if (current->free)
        {
            free_bytes += current->size;
        }
        else
        {
            used_bytes += current->size;
        }

        blocks++;
        current = current->next;
    }

    uart_puts("heap used=");
    uart_put_u64(used_bytes);
    uart_puts(" free=");
    uart_put_u64(free_bytes);
    uart_puts(" blocks=");
    uart_put_u64(blocks);
    uart_puts("\n");
}