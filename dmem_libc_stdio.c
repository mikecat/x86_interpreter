#include <stdio.h>
#include <stdlib.h>
#include "dynamic_memory.h"
#include "dmem_utils.h"
#include "dmem_libc_stdio.h"

static uint32_t iob_addr;

int dmem_libc_stdio_initialize(uint32_t iob_addr_in) {
	iob_addr = iob_addr_in;
	return 1;
}

int dmem_libc_fputs(uint32_t* ret, uint32_t esp) {
	uint32_t str_ptr, fp;
	char* str;
	if (!dmem_get_args(esp, 2, &str_ptr, &fp)) return 0;

	str = dmem_read_string(str_ptr);
	if (str == NULL) return 0;

	if (fp == iob_addr + 32 * 1) {
		*ret = fputs(str, stdout) >= 0 ? 1 : -1;
	} else if (fp == iob_addr + 32 * 2) {
		*ret = fputs(str, stderr) >= 0 ? 1 : -1;
	} else {
		*ret = -1;
	}
	free(str);
	return 1;
}

int dmem_libc_puts(uint32_t* ret, uint32_t esp) {
	uint32_t ptr;
	char* str;
	if (!dmem_get_args(esp, 1, &ptr)) return 0;

	str = dmem_read_string(ptr);
	if (str == NULL) {
		return 0;
	} else {
		int putslet = puts(str);
		free(str);
		*ret = putslet >= 0 ? 1 : -1;
		return 1;
	}
}
