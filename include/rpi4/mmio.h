#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

void mmio_write(uintptr_t reg, uint32_t val);
uint32_t mmio_read(uintptr_t reg);

#endif