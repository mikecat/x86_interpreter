#ifndef DMEM_UTILS_H_GUARD_C9C44763_2A50_4C11_BF78_9729BDE6437A
#define DMEM_UTILS_H_GUARD_C9C44763_2A50_4C11_BF78_9729BDE6437A

#include <stdint.h>

int dmem_write_uint(uint32_t addr, uint32_t value, int size);
uint32_t dmem_read_uint(int* ok, uint32_t addr, int size);
char* dmem_read_string(uint32_t addr);
int dmem_get_args(uint32_t esp, int num, ...);

#endif
