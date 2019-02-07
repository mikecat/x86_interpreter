#ifndef PE_LIBS_H_GUARD_B697AD19_9017_4B08_B4AB_7906D3493DA3
#define PE_LIBS_H_GUARD_B697AD19_9017_4B08_B4AB_7906D3493DA3

#include <stdint.h>

#define PE_LIB_EXEC_FAILED UINT32_C(0xffffffff)
#define PE_LIB_EXEC_EXIT UINT32_C(0xfffffffe)

int pe_libs_initialize(uint32_t work_start, uint32_t argc, uint32_t argv);

/* 帰る時にスタックから消すサイズを返す。実行失敗時は0xffffffffを返す。 */
/* func_ordはfunc_nameがNULLの時に使用する */
uint32_t pe_lib_exec(uint32_t regs[], const char* lib_name, const char* func_name, uint16_t func_ord);

#endif
