#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dynamic_memory.h"
#include "dmem_utils.h"
#include "dmem_libc_stdio.h"

static uint32_t iob_addr;

int dmem_libc_stdio_initialize(uint32_t iob_addr_in) {
	iob_addr = iob_addr_in;
	return 1;
}

static uint32_t printf_core(char** ret, uint32_t format_ptr, uint32_t data_prev_ptr) {
	char* format, *itr;
	char* result = NULL;
	uint32_t result_len = 0;
	*ret = NULL;
	format = dmem_read_string(format_ptr);
	if (format == NULL) return 0;

#define REALLOC_RESULT(delta) \
	if (UINT32_MAX - (delta) < result_len || \
	(next_result = realloc(result, result_len + (delta))) == NULL) { \
		free(format); \
		free(result); \
		return 0; \
	} \
	result = next_result;

	itr = format;
	for (;;) {
		char* next_result;
		if (*itr == '%') {
			REALLOC_RESULT(1)
			result[result_len++] = '%';
			itr++;
		} else {
			char* itr2 = itr;
			while (*itr2 != '%' && *itr2 != '\0') itr2++;
			REALLOC_RESULT(itr2 - itr)
			memcpy(&result[result_len], itr, itr2 - itr);
			result_len += itr2 - itr;
			if (*itr2 == '\0') {
				REALLOC_RESULT(1)
				result[result_len] = '\0';
				break;
			} else {
				itr = itr2;
			}
		}
	}
	free(format);
	*ret = result;
	return result_len;
#undef REALLOC_RESULT
}

int dmem_libc_fflush(uint32_t* ret, uint32_t esp) {
	uint32_t fp;
	FILE* fp_use;
	if (!dmem_get_args(esp, 1, &fp)) return 0;

	if (fp == 0) {
		fp_use = NULL;
	} else if (fp == iob_addr + 32 * 1) {
		fp_use = stdout;
	} else if (fp == iob_addr + 32 * 2) {
		fp_use = stderr;
	} else {
		*ret = -1;
		return 1;
	}
	*ret = fflush(fp_use) == 0 ? 0 : -1;
	return 1;
}

int dmem_libc_fprintf(uint32_t* ret, uint32_t esp) {
	uint32_t fp, format_ptr;
	char* result;
	uint32_t result_len;
	FILE* fp_use;
	if (!dmem_get_args(esp, 2, &fp, &format_ptr)) return 0;

	if (fp == iob_addr + 32 * 1) {
		fp_use = stdout;
	} else if (fp == iob_addr + 32 * 2) {
		fp_use = stderr;
	} else {
		*ret = -1;
		return 1;
	}
	result_len = printf_core(&result, format_ptr, esp + 8);
	if (result == NULL) {
		return 0;
	} else {
		if (fputs(result, fp_use) < 0) {
			*ret = -1;
		} else {
			*ret = result_len;
		}
		free(result);
		return 1;
	}
}

int dmem_libc_printf(uint32_t* ret, uint32_t esp) {
	uint32_t format_ptr;
	char* result;
	uint32_t result_len;
	if (!dmem_get_args(esp, 1, &format_ptr)) return 0;

	result_len = printf_core(&result, format_ptr, esp + 4);
	if (result == NULL) {
		return 0;
	} else {
		if (fputs(result, stdout) < 0) {
			*ret = -1;
		} else {
			*ret = result_len;
		}
		free(result);
		return 1;
	}
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

int dmem_libc_vfprintf(uint32_t* ret, uint32_t esp) {
	uint32_t fp, format_ptr, vargs;
	char* result;
	uint32_t result_len;
	FILE* fp_use;
	if (!dmem_get_args(esp, 3, &fp, &format_ptr, &vargs)) return 0;

	if (fp == iob_addr + 32 * 1) {
		fp_use = stdout;
	} else if (fp == iob_addr + 32 * 2) {
		fp_use = stderr;
	} else {
		*ret = -1;
		return 1;
	}
	result_len = printf_core(&result, format_ptr, vargs);
	if (result == NULL) {
		return 0;
	} else {
		if (fputs(result, fp_use) < 0) {
			*ret = -1;
		} else {
			*ret = result_len;
		}
		free(result);
		return 1;
	}
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