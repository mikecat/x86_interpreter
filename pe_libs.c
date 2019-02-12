#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "dynamic_memory.h"
#include "dmem_utils.h"
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
	if (strcmp(func_name, "__set_app_type") == 0) {
		/* 無視 */
		return 0;
	} else if (strcmp(func_name, "__getmainargs") == 0) {
		uint32_t esp = regs[ESP];
		uint32_t p_argc, p_argv, p_env;
		int ok1 = 0, ok2 = 0, ok3 = 0;
		int fail = 0;
		p_argc = dmem_read_uint(&ok1, esp + 4, 4);
		p_argv = dmem_read_uint(&ok2, esp + 8, 4);
		p_env = dmem_read_uint(&ok3, esp + 12, 4);
		fail = !(ok1 && ok2 && ok3);
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
		uint32_t ptr;
		int ok;
		char* str;
		ptr = dmem_read_uint(&ok, regs[ESP] + 4, 4);
		if (!ok) {
			regs[EAX] = -1;
			return 0;
		}
		str = dmem_read_string(ptr);
		if (str == NULL) {
			regs[EAX] = -1;
		} else {
			puts(str);
			free(str);
			regs[EAX] = 1;
		}
		return 0;
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
		int ok1, ok2;
		chr = dmem_read_uint(&ok1, regs[ESP] + 4, 4);
		fp = dmem_read_uint(&ok2, regs[ESP] + 8, 4);
		if (!(ok1 && ok2)) {
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
		uint32_t str_ptr, fp;
		int ok1, ok2;
		char* str;
		str_ptr = dmem_read_uint(&ok1, regs[ESP] + 4, 4);
		fp = dmem_read_uint(&ok2, regs[ESP] + 8, 4);
		if (!(ok1 && ok2)) {
			regs[EAX] = -1;
			return 0;
		}
		str = dmem_read_string(str_ptr);
		if (str == NULL) {
			regs[EAX] = -1;
			return 0;
		}
		if (fp == WORK_IOB + 32 * 1) {
			fputs(str, stdout);
			regs[EAX] = 1;
		} else if (fp == WORK_IOB + 32 * 2) {
			fputs(str, stderr);
			regs[EAX] = 1;
		} else {
			regs[EAX] = -1;
		}
		free(str);
		return 0;
	} else if (strcmp(func_name, "strchr") == 0) {
		uint32_t str_ptr, target;
		int ok1, ok2;
		char* str;
		char* res;
		str_ptr = dmem_read_uint(&ok1, regs[ESP] + 4, 4);
		target = dmem_read_uint(&ok2, regs[ESP] + 8, 4);
		if (!(ok1 && ok2)) {
			regs[EAX] = -1;
			return 0;
		}
		str = dmem_read_string(str_ptr);
		if (str == NULL) {
			regs[EAX] = 0;
			return 0;
		}
		res = strchr(str, target);
		if (res == NULL) {
			regs[EAX] = 0;
		} else {
			regs[EAX] = str_ptr + (res - str);
		}
		free(str);
		return 0;
	} else if (strcmp(func_name, "strncmp") == 0) {
		uint32_t sptr1, sptr2, n;
		int ok1, ok2, ok3;
		uint32_t i;
		sptr1 = dmem_read_uint(&ok1, regs[ESP] + 4, 4);
		sptr2 = dmem_read_uint(&ok2, regs[ESP] + 8, 4);
		n = dmem_read_uint(&ok3, regs[ESP] + 12, 4);
		if (!(ok1 && ok2 && ok3)) {
			regs[EAX] = -1;
			return 0;
		}
		for (i = 0; i < n; i++) {
			uint32_t c1, c2;
			if (UINT32_MAX - sptr1 < i || UINT32_MAX - sptr2 < i) {
				break;
			}
			c1 = dmem_read_uint(&ok1, sptr1 + i, 1);
			c2 = dmem_read_uint(&ok2, sptr2 + i, 1);
			if (!(ok1 && ok2)) {
				break;
			}
			if (c1 > c2) {
				regs[EAX] = 1;
				return 0;
			} else if (c1 < c2) {
				regs[EAX] = -1;
				return 0;
			} else if (c1 == 0) { /* c1 == c2 */
				break;
			}
		}
		regs[EAX] = 0;
		return 0;
	} else if (strcmp(func_name, "strlen") == 0) {
		uint32_t str_ptr;
		int ok;
		uint32_t i;
		str_ptr = dmem_read_uint(&ok, regs[ESP] + 4, 4);
		if (!ok) {
			regs[EAX] = 0;
			return 0;
		}
		i = 0;
		for (;;) {
			uint32_t c = dmem_read_uint(&ok, str_ptr + i, 1);
			if (!ok || c == 0 || (i == UINT32_MAX || UINT32_MAX - i - 1 < str_ptr)) {
				regs[EAX] = i;
				return 0;
			}
			i++;
		}
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
		int ok;
		msgid = dmem_read_uint(&ok, regs[ESP] + 4, 4);
		if (!ok) {
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
