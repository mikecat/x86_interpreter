#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "dynamic_memory.h"
#include "x86_regs.h"
#include "pe_libs.h"

static uint32_t work_origin;
static uint32_t argc_value, argv_value;

#define WORK_ARGV0 (work_origin + UINT32_C(0x00000000))
#define WORK_ARGV1 (work_origin + UINT32_C(0x00000004))
#define WORK_ENV0 (work_origin + UINT32_C(0x00000008))
#define WORK_PNAME (work_origin + UINT32_C(0x0000000c))
#define WORK_FMODE (work_origin + UINT32_C(0x00000010))
#define WORK_SIZE UINT32_C(0x00000014)

static int dmem_write_value(uint32_t addr, uint32_t value, int size) {
	uint8_t buffer[4];
	int i;
	if (size < 0 || 4 < size) return 0;
	if (!dmemory_is_allocated(addr, size)) return 0;
	for (i = 0; i < size; i++) buffer[i] = (value >> (8 * i)) & 0xff;
	dmemory_write(buffer, addr, size);
	return 1;
}

static uint32_t dmem_read_value(int* ok, uint32_t addr, int size) {
	uint8_t buffer[4];
	uint32_t res = 0;
	int i;
	if (ok != NULL) *ok = 0;
	if (size < 0 || 4 < size) return 0;
	if (!dmemory_is_allocated(addr, size)) return 0;
	dmemory_read(buffer, addr ,size);
	for (i = 0; i < size; i++) res |= buffer[i] << (8 * i);
	if (ok != NULL) *ok = 1;
	return res;
}

int pe_libs_initialize(uint32_t work_start, uint32_t argc, uint32_t argv) {
	if (UINT32_MAX - work_start < WORK_SIZE - 1) {
		fprintf(stderr, "no enough space for PE work\n");
		return 0;
	}
	work_origin = work_start;
	dmemory_allocate(work_origin, WORK_SIZE);
	if (argc == 0) { /* ダミーの引数情報を利用 */
		argc_value = 1;
		argv_value = WORK_ARGV0;
	} else { /* 構築された引数情報を利用 */
		argc_value = argc;
		argv_value = argv;
	}
	dmem_write_value(WORK_ARGV0, WORK_PNAME, 4);
	dmem_write_value(WORK_ARGV1, 0, 4);
	dmem_write_value(WORK_ENV0, 0, 4);
	dmemory_write("x\0\0\0", WORK_PNAME, 4);
	dmem_write_value(WORK_FMODE, 0x00004000, 4); /* O_TEXT */
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
	} else if (strcmp(func_name, "__getmainargs") == 0) {
		uint32_t esp = regs[ESP];
		uint32_t p_argc, p_argv, p_env;
		int ok1 = 0, ok2 = 0, ok3 = 0;
		int fail = 0;
		p_argc = dmem_read_value(&ok1, esp + 4, 4);
		p_argv = dmem_read_value(&ok2, esp + 8, 4);
		p_env = dmem_read_value(&ok3, esp + 12, 4);
		fail = !(ok1 && ok2 && ok3);
		fail = fail || !dmem_write_value(p_argc, argc_value, 4);
		fail = fail || !dmem_write_value(p_argv, argv_value, 4);
		fail = fail || !dmem_write_value(p_env, WORK_ENV0, 4);
		regs[EAX] = fail ? -1 : 0;
		return 0;
	} else if (strcmp(func_name, "__p__fmode") == 0) {
		regs[EAX] = WORK_FMODE;
		return 0;
	} else if (strcmp(func_name, "atexit") == 0) {
		regs[EAX] = 1;
		return 0;
	} else if (strcmp(func_name, "__p__environ") == 0) {
		regs[EAX] = WORK_ENV0;
		return 0;
	} else if (strcmp(func_name, "puts") == 0) {
		uint32_t ptr;
		uint32_t chr;
		int ok;
		ptr = dmem_read_value(&ok, regs[ESP] + 4, 4);
		if (!ok) {
			regs[EAX] = -1;
			return 0;
		}
		for (;;) {
			chr = dmem_read_value(&ok, ptr, 1);
			if (!ok) {
				regs[EAX] = -1;
				return 0;
			}
			if (chr == 0) break;
			putchar(chr);
			ptr++;
		}
		putchar('\n');
		regs[EAX] = 1;
		return 0;
	} else if (strcmp(func_name, "_cexit") == 0) {
		/* atexitで登録した関数を実行 */
		/* バッファをフラッシュ */
		/* ストリームを閉じる */
		return 0;
	} else {
		fprintf(stderr, "unimplemented function %s() in msvcrt.dll called.\n", func_name);
		return PE_LIB_EXEC_FAILED;
	}
}

static uint32_t exec_kernel32(uint32_t regs[], const char* func_name) {
	if (strcmp(func_name, "SetUnhandledExceptionFilter") == 0) {
		/* 無視 */
		regs[EAX] = 0;
		return 4;
	} else if (strcmp(func_name, "GetModuleHandleA") == 0) {
		regs[EAX] = 0;
		return 0;
	} else {
		fprintf(stderr, "unimplemented function %s() in kernel32.dll called.\n", func_name);
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
	} else if (strcmp_ncs(lib_name, "kernel32.dll") == 0) {
		return exec_kernel32(regs, func_name);
	} else {
		fprintf(stderr, "function %s() in unknown library %s called.\n", func_name, lib_name);
		return PE_LIB_EXEC_FAILED;
	}
}
