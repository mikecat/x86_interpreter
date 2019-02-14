#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "dynamic_memory.h"
#include "dmem_utils.h"
#include "dmem_libc_stdio.h"

/* 32 * 128 = 4096 */
#define BYTE_PER_IOB_FILE 32
#define IOB_SIZE 128

typedef struct {
	FILE* fp;
	int is_standard;
	int can_read;
	int can_write;
	enum pop_t {
		POP_NONE,
		POP_READ,
		POP_WRITE,
		POP_SEEK
	} previous_operation;
} file_info_t;

static uint32_t iob_addr;
static file_info_t file_info[IOB_SIZE];

#define FILE_INFO_IDX_STDIN 0
#define FILE_INFO_IDX_STDOUT 1
#define FILE_INFO_IDX_STDERR 2
#define FILE_INFO_IDX_USER 3

static file_info_t* file_ptr_to_info(uint32_t file_ptr) {
	uint32_t delta;
	if (file_ptr < iob_addr) return NULL;
	delta = file_ptr - iob_addr;
	if (delta % BYTE_PER_IOB_FILE != 0) return NULL;
	delta /= BYTE_PER_IOB_FILE;
	return delta < IOB_SIZE ? &file_info[delta] : NULL;
}

static uint32_t info_to_file_ptr(const file_info_t* info) {
	ptrdiff_t delta = info - file_info;
	uint32_t addr_delta;
	if (delta < 0 || UINT32_MAX / BYTE_PER_IOB_FILE < (uint32_t)delta) return 0;
	addr_delta = BYTE_PER_IOB_FILE * (uint32_t)delta;
	if (UINT32_MAX - iob_addr < addr_delta) return 0;
	return iob_addr + addr_delta;
}

int dmem_libc_stdio_initialize(uint32_t iob_addr_in) {
	int i;
	iob_addr = iob_addr_in;
	for (i = 0; i < IOB_SIZE; i++) {
		file_info[i].fp = NULL;
		file_info[i].is_standard = 0;
		file_info[i].can_read = 0;
		file_info[i].can_write = 0;
		file_info[i].previous_operation = POP_NONE;
	}
	file_info[0].fp = stdin;
	file_info[0].is_standard = 1;
	file_info[0].can_read = 1;
	file_info[1].fp = stdout;
	file_info[1].is_standard = 1;
	file_info[1].can_write = 1;
	file_info[2].fp = stderr;
	file_info[2].is_standard = 1;
	file_info[2].can_write = 1;
	return 1;
}

static uint32_t printf_core(char** ret, uint32_t format_ptr, uint32_t data_prev_ptr) {
	char* format, *itr;
	char* result = NULL;
	uint32_t result_len = 0;
	uint32_t data_addr = data_prev_ptr;
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
			char* itr2 = itr + 1;
			if (UINT32_MAX - 4 < data_addr) {
				free(format);
				free(result);
				return 0;
			}
			data_addr += 4;
			/* フラグ */
			/* 確保する長さ */
			/* 精度 */
			/* データサイズ */
			/* 変換指定 */
			if (*itr2 == 's') {
				uint32_t str_ptr;
				int ok = 0;
				char* str;
				size_t str_len;
				str_ptr = dmem_read_uint(&ok, data_addr, 4);
				if (!ok) { free(format); free(result); return 0; }
				str = dmem_read_string(str_ptr);
				if (str == NULL) { free(format); free(result); return 0; }
				str_len = strlen(str);
				if (str_len > UINT32_MAX) { free(str); free(format); free(result); return 0; }
				REALLOC_RESULT(str_len)
				memcpy(&result[result_len], str, str_len);
				result_len += str_len;
				free(str);
			} else {
				REALLOC_RESULT(itr2 - itr + 1)
				memcpy(&result[result_len], itr, itr2 - itr + 1);
				result_len += itr2 - itr + 1;
				if (*itr2 == '\0') break;
			}
			itr = itr2 + 1;
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

static int fflush_core(file_info_t* info) {
	if (info->fp == NULL) return 0;
	if (info->can_write && info->previous_operation != POP_READ) {
		return fflush(info->fp) == 0 ? 1 : 0;
	}
	/* 未定義 */
	return 1;
}

static int file_read(size_t* size_read, file_info_t* info, void* data, size_t length) {
	size_t read_size;
	if (info == NULL || info->fp == NULL || !info->can_read) {
		return 0;
	}
	read_size = fread(data, 1, length, info->fp);
	if (size_read != NULL) *size_read = read_size;
	info->previous_operation = POP_READ;
	return 1;
}

static int file_write(size_t* size_written, file_info_t* info, const void* data, size_t length) {
	size_t written_size;
	if (info == NULL || info->fp == NULL || !info->can_write) {
		return 0;
	}
	written_size = fwrite(data, 1, length, info->fp);
	if (size_written != NULL) *size_written = written_size;
	info->previous_operation = POP_WRITE;
	return 1;
}

int dmem_libc_fclose(uint32_t* ret, uint32_t esp) {
	uint32_t fp;
	file_info_t* info;
	int fflush_ok, fclose_ok;
	if (!dmem_get_args(esp, 1, &fp)) return 0;

	info = file_ptr_to_info(fp);
	if (info == NULL || info->fp == NULL) {
		*ret = -1;
		return 1;
	}
	fflush_ok = fflush_core(info);
	fclose_ok = info->is_standard || fclose(info->fp) == 0;
	info->fp = NULL;
	info->is_standard = 0;
	info->can_read = 0;
	info->can_write = 0;
	info->previous_operation = POP_NONE;
	*ret = fflush_ok && fclose_ok ? 0 : -1;
	return 1;
}

int dmem_libc_fflush(uint32_t* ret, uint32_t esp) {
	uint32_t fp;
	if (!dmem_get_args(esp, 1, &fp)) return 0;

	if (fp == 0) {
		int all_ok = 1;
		int i;
		for (i = 0; i < IOB_SIZE; i++) {
			if (file_info[i].fp != NULL && !fflush_core(&file_info[i])) all_ok = 0;
		}
		*ret = all_ok ? 0 : -1;
	} else {
		file_info_t* info = file_ptr_to_info(fp);
		if (info == NULL) {
			*ret = -1;
		} else {
			*ret = fflush_core(info) ? 0 : -1;
		}
	}
	return 1;
}

int dmem_libc_fopen(uint32_t* ret, uint32_t esp) {
	uint32_t filename_ptr, mode_ptr;
	char *filename, *mode;
	file_info_t* new_info = NULL;
	int i;
	size_t si;
	enum filemode_t {
		MODE_NONE, MODE_READ, MODE_WRITE, MODE_APPEND
	} mode_decoded = MODE_NONE;
	int is_binary = 0;
	int is_exclusive = 0;
	int is_plus = 0;
	int mode_decode_error = 0;
	char mode_str[8];
	int mode_str_idx = 0;
	uint32_t ret_ptr;
	if (!dmem_get_args(esp, 2, &filename_ptr, &mode_ptr)) return 0;
	filename = dmem_read_string(filename_ptr);
	mode = dmem_read_string(mode_ptr);
	if (filename == NULL || mode == NULL) {
		free(filename); free(mode);
		*ret = 0;
		return 1;
	}

	/* モード文字列を解釈する */
	for (si = 0; !mode_decode_error && mode[si] != '\0'; si++) {
		switch (mode[si]) {
		case 'r':
			if (mode_decoded != MODE_NONE) mode_decode_error = 1;
			mode_decoded = MODE_READ;
			break;
		case 'w':
			if (mode_decoded != MODE_NONE) mode_decode_error = 1;
			mode_decoded = MODE_WRITE;
			break;
		case 'a':
			if (mode_decoded != MODE_NONE) mode_decode_error = 1;
			mode_decoded = MODE_APPEND;
			break;
		case 'b':
			is_binary = 1;
			break;
		case 'x':
			is_exclusive = 1;
			break;
		case '+':
			is_plus = 1;
			break;
		default:
			mode_decode_error = 1;
			break;
		}
	}
	if (mode_decoded == MODE_NONE) mode_decode_error = 1;
	if (is_exclusive && mode_decoded != MODE_WRITE) mode_decode_error = 1;
	if (mode_decode_error) {
		free(filename); free(mode);
		*ret = 0;
		return 1;
	}
	/* 解釈に基づき、実行用のfopenに渡すモードを構築する */
	switch (mode_decoded) {
	case MODE_READ: mode_str[mode_str_idx++] = 'r'; break;
	case MODE_WRITE: mode_str[mode_str_idx++] = 'w'; break;
	case MODE_APPEND: mode_str[mode_str_idx++] = 'a'; break;
	default:
		free(filename); free(mode);
		*ret = 0;
		return 1;
	}
	if (is_plus) mode_str[mode_str_idx++] = '+';
	if (is_binary) mode_str[mode_str_idx++] = 'b';
	if (is_exclusive) mode_str[mode_str_idx++] = 'x';
	mode_str[mode_str_idx] = '\0';

	/* 空きエントリを探す */
	for (i = FILE_INFO_IDX_USER; i < IOB_SIZE; i++) {
		if (file_info[i].fp == NULL) {
			new_info = &file_info[i];
			break;
		}
	}
	if (new_info == NULL) {
		free(filename); free(mode);
		*ret = 0;
		return 1;
	}
	ret_ptr = info_to_file_ptr(new_info);
	if (ret_ptr == 0) {
		free(filename); free(mode);
		*ret = 0;
		return 1;
	}

	/* ファイルを開く */
	new_info->fp = fopen(filename, mode_str);
	if (new_info->fp == NULL) {
		free(filename); free(mode);
		*ret = 0;
		return 1;
	}
	new_info->is_standard = 0;
	new_info->can_read = (mode_decoded == MODE_READ || is_plus);
	new_info->can_write = (mode_decoded != MODE_READ || is_plus);
	new_info->previous_operation = POP_NONE;

	free(filename);
	free(mode);
	*ret = ret_ptr;
	return 1;
}

int dmem_libc_fprintf(uint32_t* ret, uint32_t esp) {
	uint32_t fp, format_ptr;
	char* result;
	uint32_t result_len;
	if (!dmem_get_args(esp, 2, &fp, &format_ptr)) return 0;

	result_len = printf_core(&result, format_ptr, esp + 8);
	if (result == NULL) {
		return 0;
	} else {
		size_t size_written;
		if (file_write(&size_written, file_ptr_to_info(fp), result, result_len) &&
		size_written == result_len) {
			*ret = result_len;
		} else {
			*ret = -1;
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
		size_t size_written;
		if (file_write(&size_written, &file_info[FILE_INFO_IDX_STDOUT], result, result_len) &&
		size_written == result_len) {
			*ret = result_len;
		} else {
			*ret = -1;
		}
		free(result);
		return 1;
	}
}

int dmem_libc_sprintf(uint32_t* ret, uint32_t esp) {
	uint32_t dest, format_ptr;
	char* result;
	uint32_t result_len;
	if (!dmem_get_args(esp, 2, &dest, &format_ptr)) return 0;

	result_len = printf_core(&result, format_ptr, esp + 4);
	if (result == NULL) {
		return 0;
	} else {
		if (UINT32_MAX - 1 < result_len || !dmemory_is_allocated(dest, result_len + 1)) {
			free(result);
			return 0;
		}
		dmemory_write(result, dest, result_len + 1);
		*ret = result_len;
		free(result);
		return 1;
	}
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
	result_len = printf_core(&result, format_ptr, vargs - 4);
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

int dmem_libc_fputs(uint32_t* ret, uint32_t esp) {
	uint32_t str_ptr, fp;
	char* str;
	size_t str_len, size_written;
	if (!dmem_get_args(esp, 2, &str_ptr, &fp)) return 0;

	str = dmem_read_string(str_ptr);
	if (str == NULL) return 0;

	str_len = strlen(str);
	if (file_write(&size_written, file_ptr_to_info(fp), str, str_len) &&
	size_written == str_len) {
		*ret = 1;
	} else {
		*ret = -1;
	}
	free(str);
	return 1;
}

int dmem_libc_puts(uint32_t* ret, uint32_t esp) {
	uint32_t ptr;
	char* str;
	size_t str_len, size_written;
	if (!dmem_get_args(esp, 1, &ptr)) return 0;

	str = dmem_read_string(ptr);
	if (str == NULL) return 0;

	str_len = strlen(str);
	str[str_len] = '\n';
	if (file_write(&size_written, &file_info[FILE_INFO_IDX_STDOUT], str, str_len + 1) &&
	size_written == str_len) {
		*ret = 1;
	} else {
		*ret = -1;
	}
	free(str);
	return 1;
}

int dmem_libc_fread(uint32_t* ret, uint32_t esp) {
	uint32_t dest, elem_size, num, fp;
	uint32_t all_size;
	char* buffer;
	size_t read_size;
	if (!dmem_get_args(esp, 4, &dest, &elem_size, &num, &fp)) return 0;
	if (elem_size == 0 || num == 0) {
		/* 何もしない */
		*ret = 0;
		return 1;
	}
	if (UINT32_MAX / elem_size < num) {
		*ret = 0;
		return 1;
	}
	all_size = elem_size * num;
	if (!dmemory_is_allocated(dest, all_size)) return 0;
	buffer = malloc(all_size);
	if (buffer == NULL) {
		*ret = 0;
		return 1;
	}
	if (file_read(&read_size, file_ptr_to_info(fp), buffer, all_size)) {
		dmemory_write(buffer, dest, read_size);
		*ret = read_size / elem_size;
	} else {
		*ret = 0;
	}
	free(buffer);
	return 1;
}

int dmem_flsbuf(uint32_t* ret, uint32_t esp) {
	uint32_t chr, fp;
	uint8_t chr_buffer;
	size_t size_written;
	if (!dmem_get_args(esp, 2, &chr, &fp)) return 0;

	/* TODO: バッファの処理? */

	chr_buffer = (uint8_t)chr;
	if (file_write(&size_written, file_ptr_to_info(fp), &chr_buffer, 1) && size_written == 1) {
		*ret = chr_buffer;
	} else {
		*ret = -1; /* putchar失敗 */
	}
	return 1;
}
