#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "dynamic_memory.h"
#include "x86_regs.h"
#include "pe_libs.h"

int pe_libs_initialize(uint32_t work_start, uint32_t argc, uint32_t argv) {
	return 1;
}

/* not case sensitive */
static int strcmp_ncs(const char* a, const char* b) {
	for (;;) {
		int ac = tolower((unsigned char)*a);
		int bc = tolower((unsigned char)*b);
		if (ac != bc) return ac > bc ? 1 : -1;
		if (ac == 0) return 0;
		a++;
		b++;
	}
}

static uint32_t exec_msvcrt(uint32_t regs[], const char* func_name) {
	if (strcmp(func_name, "__set_app_type") == 0) {
		/* 無視 */
		return 0;
	} else {
		fprintf(stderr, "unimplemented function %s() in msvcrt.dll called.\n", func_name);
		return PE_LIB_EXEC_FAILED;
	}
}

uint32_t pe_lib_exec(uint32_t regs[], const char* lib_name, const char* func_name, uint16_t func_ord) {
	if (func_name == NULL) {
		fprintf(stderr, "function #%"PRIu16" in library %s called. (ord value unsupported)\n",
			func_ord, lib_name);
		return PE_LIB_EXEC_FAILED;
	}
	if (strcmp_ncs(lib_name, "msvcrt.dll") == 0) {
		return exec_msvcrt(regs, func_name);
	} else {
		fprintf(stderr, "function %s() in unknown library %s called.\n", func_name, lib_name);
		return PE_LIB_EXEC_FAILED;
	}
}
