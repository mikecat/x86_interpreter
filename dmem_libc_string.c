#include <stdlib.h>
#include <string.h>
#include "dmem_libc_string.h"
#include "dynamic_memory.h"
#include "dmem_utils.h"

int dmem_libc_string_initialize(void) {
	return 1;
}

int dmem_libc_strcpy(uint32_t* ret, uint32_t esp) {
	uint32_t dest, src;
	char* str;
	size_t str_len;
	if (!dmem_get_args(esp, 2, &dest, &src)) return 0;

	str = dmem_read_string(src);
	if (str == NULL) return 0;
	str_len = strlen(str);
	if (UINT32_MAX - 1 < str_len) return 0;
	if (!dmemory_is_allocated(dest, (uint32_t)str_len + 1)) return 0;
	dmemory_write(str, dest, (uint32_t)str_len + 1);
	*ret = dest;
	return 1;
}

int dmem_libc_strcmp(uint32_t* ret, uint32_t esp) {
	uint32_t sptr1, sptr2;
	uint32_t i;
	if (!dmem_get_args(esp, 2, &sptr1, &sptr2)) return 0;

	for (i = 0; ; i++) {
		uint32_t c1, c2;
		int ok1, ok2;
		if (UINT32_MAX - sptr1 < i || UINT32_MAX - sptr2 < i) return 0;
		c1 = dmem_read_uint(&ok1, sptr1 + i, 1);
		c2 = dmem_read_uint(&ok2, sptr2 + i, 1);
		if (!(ok1 && ok2)) return 0;
		if (c1 > c2) {
			*ret = 1;
			return 1;
		} else if (c1 < c2) {
			*ret = -1;
			return 1;
		} else if (c1 == 0) { /* c1 == c2 */
			*ret = 0;
			return 1;
		}
		if (i == UINT32_MAX) return 0;
	}
}

int dmem_libc_strncmp(uint32_t* ret, uint32_t esp) {
	uint32_t sptr1, sptr2, n;
	uint32_t i;
	if (!dmem_get_args(esp, 3, &sptr1, &sptr2, &n)) return 0;

	for (i = 0; i < n; i++) {
		uint32_t c1, c2;
		int ok1, ok2;
		if (UINT32_MAX - sptr1 < i || UINT32_MAX - sptr2 < i) return 0;
		c1 = dmem_read_uint(&ok1, sptr1 + i, 1);
		c2 = dmem_read_uint(&ok2, sptr2 + i, 1);
		if (!(ok1 && ok2)) return 0;
		if (c1 > c2) {
			*ret = 1;
			return 1;
		} else if (c1 < c2) {
			*ret = -1;
			return 1;
		} else if (c1 == 0) { /* c1 == c2 */
			break;
		}
		if (i == UINT32_MAX) return 0;
	}
	*ret = 0;
	return 1;
}

int dmem_libc_strchr(uint32_t* ret, uint32_t esp) {
	uint32_t str_ptr, target;
	char* str;
	char* res;
	if (!dmem_get_args(esp, 2, &str_ptr, &target)) return 0;

	str = dmem_read_string(str_ptr);
	if (str == NULL) return 0;

	res = strchr(str, target);
	if (res == NULL) {
		*ret = 0;
	} else {
		*ret = str_ptr + (res - str);
	}
	free(str);
	return 1;
}

int dmem_libc_strlen(uint32_t* ret, uint32_t esp) {
	uint32_t str_ptr;
	uint32_t i;
	if (!dmem_get_args(esp, 1, &str_ptr)) return 0;

	i = 0;
	for (;;) {
		int ok;
		uint32_t c = dmem_read_uint(&ok, str_ptr + i, 1);
		if (!ok) return 0;
		if (c == 0) {
			*ret = i;
			return 1;
		}
		if (i == UINT32_MAX || UINT32_MAX - i - 1 < str_ptr) return 0;
		i++;
	}
}
