#ifndef DMEM_LIBC_TIME_H_GUARD_BC438B62_33B4_4B18_8BDF_816472AD2787
#define DMEM_LIBC_TIME_H_GUARD_BC438B62_33B4_4B18_8BDF_816472AD2787

#include <stdint.h>

int dmem_libc_time_initialize(uint32_t buffer_start_addr);

int dmem_libc_localtime(uint32_t* ret, uint32_t esp);
int dmem_libc_strftime(uint32_t* ret, uint32_t esp);

#endif
