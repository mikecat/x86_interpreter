#ifndef DMEM_LIBC_STDLIB_H_GUARD_A1E97A66_E562_4B10_B55F_C9DB80FDE5D2
#define DMEM_LIBC_STDLIB_H_GUARD_A1E97A66_E562_4B10_B55F_C9DB80FDE5D2

#include <stdint.h>

int dmem_libc_stdlib_initialize(uint32_t heap_start_addr);

int dmem_libc_free(uint32_t* ret, uint32_t esp);
int dmem_libc_malloc(uint32_t* ret, uint32_t esp);
int dmem_libc_realloc(uint32_t* ret, uint32_t esp);

#endif
