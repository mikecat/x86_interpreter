#ifndef PE_IMPORT_H_GUARD_6FA7674E_5527_4DF1_8BE1_13B3365EB3D8
#define PE_IMPORT_H_GUARD_6FA7674E_5527_4DF1_8BE1_13B3365EB3D8

#include <stdint.h>

typedef struct {
	uint32_t image_base;
	uint32_t import_addr, import_size;
	uint32_t iat_addr, iat_size;
} pe_import_params;

int pe_import_initialize(const pe_import_params* params, uint32_t work_start, uint32_t argc, uint32_t argv);

/* 成功:1 失敗:-1 プログラム終了(成功):0 */
int pe_import(uint32_t* eip, uint32_t regs[]);

#endif
