#ifndef DMEM_UTILS_H_GUARD_C9C44763_2A50_4C11_BF78_9729BDE6437A
#define DMEM_UTILS_H_GUARD_C9C44763_2A50_4C11_BF78_9729BDE6437A

#include <stdint.h>

int dmem_write_value(uint32_t addr, uint32_t value, int size);
uint32_t dmem_read_value(int* ok, uint32_t addr, int size);
char* read_string_dmem(uint32_t addr);

#endif
