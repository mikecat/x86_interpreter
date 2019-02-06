#ifndef READ_PE_H_GUARD_17D018F2_EACE_450B_BF0A_82C30EA73FF1
#define READ_PE_H_GUARD_17D018F2_EACE_450B_BF0A_82C30EA73FF1

#include <stdint.h>

int read_pe(uint32_t* eip_value, uint32_t* stack_size, const char* filename);

#endif
