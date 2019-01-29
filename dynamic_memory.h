#ifndef DYNAMIC_MEMORY_H_GUARD_E5626326_5B1D_4697_B74A_FDA0F2F84E83
#define DYNAMIC_MEMORY_H_GUARD_E5626326_5B1D_4697_B74A_FDA0F2F84E83

#include <stdint.h>

void dmemory_read(void* dest, uint32_t addr, uint32_t size);
void dmemory_write(void* src, uint32_t addr, uint32_t size);
void dmemory_allocate(uint32_t addr, uint32_t size);
void dmemory_deallocate(uint32_t addr, uint32_t size);
int dmemory_is_allocated(uint32_t addr, uint32_t size);

#endif
