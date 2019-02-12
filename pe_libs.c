#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "dynamic_memory.h"
#include "dmem_utils.h"
#include "dmem_libc.h"
#include "x86_regs.h"
#include "pe_libs.h"

static uint32_t work_origin;
static uint32_t argc_value, argv_value;

#define WORK_ARGV0 (work_origin + UINT32_C(0x00000000))
#define WORK_ARGV1 (work_origin + UINT32_C(0x00000004))
#define WORK_ENV0 (work_origin + UINT32_C(0x00000008))
#define WORK_PNAME (work_origin + UINT32_C(0x0000000c))
#define WORK_FMODE (work_origin + UINT32_C(0x00000010))
#define WORK_IOB (work_origin + UINT32_C(0x00001000))
#define WORK_SIZE UINT32_C(0x00002000)

enum {
	LIB_ID_UNKNOWN,
	LIB_ID_MSVCRT
};

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
	dmem_write_uint(WORK_ARGV0, WORK_PNAME, 4);
	dmem_write_uint(WORK_ARGV1, 0, 4);
	dmem_write_uint(WORK_ENV0, 0, 4);
	dmemory_write("x\0\0\0", WORK_PNAME, 4);
	dmem_write_uint(WORK_FMODE, 0x00004000, 4); /* O_TEXT */

	if (!dmem_libc_stdio_initialize(WORK_IOB)) return 0;
	if (!dmem_libc_string_initialize()) return 0;
	return 1;
}

int get_lib_id(const char* lib_name) {
	if (strcmp_ncs(lib_name, "msvcrt.dll") == 0) return LIB_ID_MSVCRT;
	return LIB_ID_UNKNOWN;
}

uint32_t get_buffer_address(int lib_id, const char* identifier, uint32_t default_addr) {
	if (identifier == NULL) return default_addr;
	switch (lib_id) {
	case LIB_ID_MSVCRT:
		if (strcmp(identifier, "_iob") == 0) {
			return WORK_IOB;
		}
		break;
	}
	return default_addr;
}

static uint32_t exec_msvcrt(uint32_t regs[], const char* func_name) {
#define CALL_DMEM_LIBC(name) \
	if (dmem_libc_ ## name (&regs[EAX], regs[ESP])) { \
		return 0; \
	} else { \
		fprintf(stderr, "failure in executing " #name "() in msvcrt.dll\n"); \
		return PE_LIB_EXEC_FAILED; \
	}

	if (strcmp(func_name, "__set_app_type") == 0) {
		/* 無視 */
		return 0;
	} else if (strcmp(func_name, "__getmainargs") == 0) {
		uint32_t p_argc, p_argv, p_env;
		int fail = 0;
		fail = !dmem_get_args(regs[ESP], 3, &p_argc, &p_argv, &p_env);
		fail = fail || !dmem_write_uint(p_argc, argc_value, 4);
		fail = fail || !dmem_write_uint(p_argv, argv_value, 4);
		fail = fail || !dmem_write_uint(p_env, WORK_ENV0, 4);
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
		CALL_DMEM_LIBC(puts)
	} else if (strcmp(func_name, "_cexit") == 0) {
		/* atexitで登録した関数を実行 */
		/* バッファをフラッシュ */
		/* ストリームを閉じる */
		return 0;
	} else if (strcmp(func_name, "getenv") == 0) {
		regs[EAX] = 0;
		return 0;
	} else if (strcmp(func_name, "setlocale") == 0) {
		regs[EAX] = 0;
		return 0;
	} else if (strcmp(func_name, "_flsbuf") == 0) {
		uint32_t chr, fp;
		if (!dmem_get_args(regs[ESP], 2, &chr, &fp)) {
			regs[EAX] = -1;
			return 0;
		}
		/* TODO: バッファの処理? */
		if (fp == WORK_IOB + 32 * 1) {
			fputc(chr, stdout);
			regs[EAX] = chr;
		} else if (fp == WORK_IOB + 32 * 2) {
			fputc(chr, stderr);
			regs[EAX] = chr;
		} else {
			regs[EAX] = -1; /* putchar失敗 */
		}
		return 0;
	} else if (strcmp(func_name, "exit") == 0) {
		/* atexitで登録した関数を実行 */
		/* バッファをフラッシュ */
		/* ストリームを閉じる */
		return PE_LIB_EXEC_EXIT;
	} else if (strcmp(func_name, "fputs") == 0) {
		CALL_DMEM_LIBC(fputs)
	} else if (strcmp(func_name, "strchr") == 0) {
		CALL_DMEM_LIBC(strchr)
	} else if (strcmp(func_name, "strncmp") == 0) {
		CALL_DMEM_LIBC(strncmp)
	} else if (strcmp(func_name, "strlen") == 0) {
		CALL_DMEM_LIBC(strlen)
	} else if (strcmp(func_name, "printf") == 0) {
		CALL_DMEM_LIBC(printf)
	} else if (strcmp(func_name, "fprintf") == 0) {
		CALL_DMEM_LIBC(fprintf)
	} else if (strcmp(func_name, "vfprintf") == 0) {
		CALL_DMEM_LIBC(vfprintf)
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
	} else if (strcmp(func_name, "ExitProcess") == 0) {
		return PE_LIB_EXEC_EXIT;
	} else {
		fprintf(stderr, "unimplemented function %s() in kernel32.dll called.\n", func_name);
		return PE_LIB_EXEC_FAILED;
	}
}

static uint32_t exec_libintl3(uint32_t regs[], const char* func_name) {
	if (strcmp(func_name, "libintl_bindtextdomain") == 0) {
		regs[EAX] = 0;
		return 0;
	} else if (strcmp(func_name, "libintl_textdomain") == 0) {
		regs[EAX] = 0;
		return 0;
	} else if (strcmp(func_name, "libintl_gettext") == 0) {
		uint32_t msgid;
		if (!dmem_get_args(regs[ESP], 1, &msgid)) {
			regs[EAX] = 0;
		} else {
			regs[EAX] = msgid;
		}
		return 0;
	} else {
		fprintf(stderr, "unimplemented function %s() in libintl3.dll called.\n", func_name);
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
	} else if (strcmp_ncs(lib_name, "libintl3.dll") == 0) {
		return exec_libintl3(regs, func_name);
	} else {
		fprintf(stderr, "function %s() in unknown library %s called.\n", func_name, lib_name);
		return PE_LIB_EXEC_FAILED;
	}
}
