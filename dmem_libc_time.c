#include <stdlib.h>
#include "dmem_libc_time.h"
#include "dynamic_memory.h"
#include "dmem_utils.h"

static uint32_t buffer_start;
#define LOCALTIME_TM (buffer_start + UINT32_C(0)) /* 36 (0x24) bytes */
#define BUFFER_SIZE UINT32_C(0x24)

int dmem_libc_time_initialize(uint32_t buffer_start_addr) {
	if (!dmemory_is_allocated(buffer_start_addr, BUFFER_SIZE)) return 0;
	buffer_start = buffer_start_addr;
	return 1;
}

int dmem_libc_localtime(uint32_t* ret, uint32_t esp) {
	uint32_t time_addr;
	uint32_t time_val;
	int ok;
	if (!dmem_get_args(esp, 1, &time_addr)) return 0;

	time_val = dmem_read_uint(&ok, time_addr, 4);
	if (!ok) {
		*ret = 0;
		return 1;
	}

	dmem_write_uint(LOCALTIME_TM + 0, 0, 4); /* tm_sec */
	dmem_write_uint(LOCALTIME_TM + 4, 34, 4); /* tm_min */
	dmem_write_uint(LOCALTIME_TM + 8, 3, 4); /* tm_hour */
	dmem_write_uint(LOCALTIME_TM + 12, 1, 4); /* tm_mday */
	dmem_write_uint(LOCALTIME_TM + 16, 0, 4); /* tm_mon */
	dmem_write_uint(LOCALTIME_TM + 20, 100, 4); /* tm_year */
	dmem_write_uint(LOCALTIME_TM + 24, 0, 4); /* tm_wday */
	dmem_write_uint(LOCALTIME_TM + 28, 0, 4); /* tm_yday */
	dmem_write_uint(LOCALTIME_TM + 32, 0, 4); /* tm_isdst */

	*ret = LOCALTIME_TM;
	return 1;
}

int dmem_libc_strftime(uint32_t* ret, uint32_t esp) {
	uint32_t out_ptr, out_max, format_ptr, tm_ptr;
	char* format;
	char* out_data;
	uint32_t out_count = 0;
	size_t i;
	if (!dmem_get_args(esp, 4, &out_ptr, &out_max, &format_ptr, &tm_ptr)) return 0;
	if (!dmemory_is_allocated(out_ptr, out_max)) return 0;

	format = dmem_read_string(format_ptr);
	if (format == NULL) return 0;
	out_data = malloc(out_max);
	if (out_data == NULL) {
		free(format);
		return 0;
	}

	for (i = 0; out_count < out_max && format[i] != '\0'; i++) {
		out_data[out_count++] = format[i];
	}

	if (out_count < out_max) {
		out_data[out_count] = '\0';
		dmemory_write(out_data, out_ptr, out_count + 1);
		*ret = out_count;
	} else {
		*ret = 0;
	}

	free(format);
	free(out_data);
	return 1;
}
