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
	uint64_t time_left;
	uint32_t wday, year, yday, month, mday;
	if (!dmem_get_args(esp, 1, &time_addr)) return 0;

	time_val = dmem_read_uint(&ok, time_addr, 4);
	if (!ok) {
		*ret = 0;
		return 1;
	}

	/* UTC → JST */
	time_left = time_val + UINT64_C(9) * 60 * 60;
	/* 曜日を求める (1970年1月1日は木曜日) */
	wday = (4 + time_left / (UINT32_C(24) * 60 * 60)) % 7;
	/* 線形探索で年を求める */
	year = 1970;
	for (;;) {
		uint32_t year_days = 365;
		uint32_t year_seconds;
		if (year % 4 == 0 && (year % 400 == 0 || year % 100 != 0)) {
			year_days++; /* うるう年 */
		}
		year_seconds = year_days * 24 * 60 * 60;
		if (time_left >= year_seconds) {
			time_left -= year_seconds;
			year++;
		} else {
			break;
		}
	}
	yday = time_left / (UINT32_C(24) * 60 * 60);
	/* 線形探索で月を求める */
	month = 0;
	for (;;) {
		uint32_t month_days, month_seconds;
		switch (month) {
		case 1: /* 2月 */
			if (year % 4 == 0 && (year % 400 == 0 || year % 100 != 0)) {
				month_days = 29; /* うるう年 */
			} else {
				month_days = 28;
			}
			break;
		case 3: case 5: case 8: case 10: /* 4,6,9,11月 */
			month_days = 30;
			break;
		default:
			month_days = 31;
			break;
		}
		month_seconds = month_days * 24 * 60 * 60;
		if (time_left >= month_seconds) {
			time_left -= month_seconds;
			month++;
		} else {
			break;
		}
	}
	mday = time_left / (UINT32_C(24) * 60 * 60) + 1;

	dmem_write_uint(LOCALTIME_TM + 0, time_left % 60, 4); /* tm_sec */
	dmem_write_uint(LOCALTIME_TM + 4, (time_left / 60) % 60, 4); /* tm_min */
	dmem_write_uint(LOCALTIME_TM + 8, (time_left / (60 * 60)) % 24, 4); /* tm_hour */
	dmem_write_uint(LOCALTIME_TM + 12, mday, 4); /* tm_mday */
	dmem_write_uint(LOCALTIME_TM + 16, month, 4); /* tm_mon */
	dmem_write_uint(LOCALTIME_TM + 20, year - 1900, 4); /* tm_year */
	dmem_write_uint(LOCALTIME_TM + 24, wday, 4); /* tm_wday */
	dmem_write_uint(LOCALTIME_TM + 28, yday, 4); /* tm_yday */
	dmem_write_uint(LOCALTIME_TM + 32, 0, 4); /* tm_isdst */

	*ret = LOCALTIME_TM;
	return 1;
}

int dmem_libc_strftime(uint32_t* ret, uint32_t esp) {
	static const char* const weekday_name_full[] = {
		"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
	};
	static const char* const weekday_name_abb[] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char* month_name_full[] = {
		"January", "February", "March", "April", "May", "June",
		"July", "August", "September", "October", "November", "December"
	};
	static const char* month_name_abb[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	uint32_t out_ptr, out_max, format_ptr, tm_ptr;
	uint32_t sec, min, hour, mday, mon, year, wday, yday;
	char* format;
	char* out_data;
	const char* format_itr = NULL;
	const char* format_itr_ret = NULL;
	int alt_format = 0;
	uint32_t out_count = 0;
	int ok;
	if (!dmem_get_args(esp, 4, &out_ptr, &out_max, &format_ptr, &tm_ptr)) return 0;
	if (!dmemory_is_allocated(out_ptr, out_max)) return 0;

	sec = dmem_read_uint(&ok, tm_ptr + 0, 4); if (!ok) return 0;
	min = dmem_read_uint(&ok, tm_ptr + 4, 4); if (!ok) return 0;
	hour = dmem_read_uint(&ok, tm_ptr + 8, 4); if (!ok) return 0;
	mday = dmem_read_uint(&ok, tm_ptr + 12, 4); if (!ok) return 0;
	mon = dmem_read_uint(&ok, tm_ptr + 16, 4); if (!ok) return 0;
	year = dmem_read_uint(&ok, tm_ptr + 20, 4); if (!ok) return 0;
	wday = dmem_read_uint(&ok, tm_ptr + 24, 4); if (!ok) return 0;
	yday = dmem_read_uint(&ok, tm_ptr + 28, 4); if (!ok) return 0;

	format = dmem_read_string(format_ptr);
	if (format == NULL) return 0;
	out_data = malloc(out_max);
	if (out_data == NULL) {
		free(format);
		return 0;
	}

	for (format_itr = format; out_count < out_max;) {
		if (*format_itr == '\0' && alt_format) {
			format_itr = format_itr_ret;
			alt_format = 0;
		}
		if (*format_itr == '\0') {
			break;
		} else if (*format_itr == '%') {
			const char* append_str = NULL;
			int do_append_int = 0;
			uint32_t append_int_digits = 0;
			uint32_t append_int = 0;
			format_itr++;
			if (*format_itr == 'E' || *format_itr == 'O') format_itr++;
			if (*format_itr == '\0') break;
			switch (*(format_itr++)) {
			case 'a':
				if (wday < 7) append_str = weekday_name_abb[wday];
				break;
			case 'A':
				if (wday < 7) append_str = weekday_name_full[wday];
				break;
			case 'b':
				if (mon < 12) append_str = month_name_abb[mon];
				break;
			case 'B':
				if (mon < 12) append_str = month_name_full[mon];
				break;
			case 'c':
				format_itr_ret = format_itr;
				format_itr = "%a %b %e %H:%M:%S %Y"; /* "%a %b %e %T %Y" with %T expanded */
				alt_format = 1;
				break;
			case 'C':
				do_append_int = 1;
				append_int_digits = 2;
				append_int = (year + 1900) / 100;
				break;
			case 'd':
				do_append_int = 1;
				append_int_digits = 2;
				append_int = mday;
				break;
			case 'D':
				format_itr_ret = format_itr;
				format_itr = "%m/%d/%y";
				alt_format = 1;
				break;
			case 'e':
				if (mday < 10) {
					out_data[out_count++] = ' ';
					if (out_count < out_max) out_data[out_count++] = '0' + mday;
				} else {
					do_append_int = 1;
					append_int_digits = 2;
					append_int = mday;
				}
				break;
			case 'F':
				format_itr_ret = format_itr;
				format_itr = "%Y-%m-%d";
				alt_format = 1;
				break;
			case 'G':
				/* TODO (not supported) */
				append_str = "%G";
				break;
			case 'h':
				format_itr_ret = format_itr;
				format_itr = "%b";
				alt_format = 1;
				break;
			case 'H':
				do_append_int = 1;
				append_int_digits = 2;
				append_int = hour;
				break;
			case 'I':
				do_append_int = 1;
				append_int_digits = 2;
				append_int = hour % 12 == 0 ? 12 : hour % 12;
				break;
			case 'j':
				do_append_int = 1;
				append_int_digits = 3;
				append_int = yday + 1;
				break;
			case 'm':
				do_append_int = 1;
				append_int_digits = 2;
				append_int = mon + 1;
				break;
			case 'M':
				do_append_int = 1;
				append_int_digits = 2;
				append_int = min;
				break;
			case 'n':
				append_str = "\n";
				break;
			case 'p':
				append_str = hour >= 12 ? "PM" : "AM";
				break;
			case 'r':
				format_itr_ret = format_itr;
				format_itr = "%I:%M:%S %p";
				alt_format = 1;
				break;
			case 'R':
				format_itr_ret = format_itr;
				format_itr = "%H:%M";
				alt_format = 1;
				break;
			case 'S':
				do_append_int = 1;
				append_int_digits = 2;
				append_int = sec;
				break;
			case 't':
				append_str = "\t";
				break;
			case 'T':
				format_itr_ret = format_itr;
				format_itr = "%H:%M:%S";
				alt_format = 1;
				break;
			case 'u':
				do_append_int = 1;
				append_int_digits = 1;
				append_int = wday == 0 ? 7 : wday;
				break;
			case 'U':
				/* TODO (not supported) */
				append_str = "%U";
				break;
			case 'V':
				/* TODO (not supported) */
				append_str = "%V";
				break;
			case 'w':
				do_append_int = 1;
				append_int_digits = 1;
				append_int = wday;
				break;
			case 'W':
				/* TODO (not supported) */
				append_str = "%W";
				break;
			case 'x':
				format_itr_ret = format_itr;
				format_itr = "%m/%d/%y";
				alt_format = 1;
				break;
			case 'X':
				format_itr_ret = format_itr;
				format_itr = "%H:%M:%S"; /* "%T" with %T expanded */
				alt_format = 1;
				break;
			case 'y':
				do_append_int = 1;
				append_int_digits = 2;
				append_int = year % 100;
				break;
			case 'Y':
				do_append_int = 1;
				append_int_digits = 2;
				append_int = year + 1900;
				break;
			case 'z':
				append_str = "+0900";
				break;
			case 'Z':
				append_str = "JST";
				break;
			case '%':
				append_str = "%";
				break;
			}
			if (append_str != NULL) {
				const char* as_ptr = append_str;
				while (*as_ptr != '\0' && out_count < out_max) {
					out_data[out_count++] = *(as_ptr++);
				}
			}
			if (do_append_int) {
				uint64_t pos = 1;
				uint32_t i;
				for (i = 0; i < append_int_digits || pos <= append_int; i++) pos *= 10;
				for (pos /= 10; out_count < out_max && pos > 0; pos /= 10) {
					out_data[out_count++] = '0' + ((uint32_t)(append_int / pos) % 10);
				}
			}
		} else {
			out_data[out_count++] = *(format_itr++);
		}
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
