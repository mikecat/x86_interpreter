#include <stdlib.h>
#include <stdarg.h>
#include "dmem_utils.h"
#include "dynamic_memory.h"

int dmem_write_uint(uint32_t addr, uint32_t value, int size) {
	uint8_t buffer[4];
	int i;
	if (size < 0 || 4 < size) return 0;
	if (!dmemory_is_allocated(addr, size)) return 0;
	for (i = 0; i < size; i++) buffer[i] = (value >> (8 * i)) & 0xff;
	dmemory_write(buffer, addr, size);
	return 1;
}

uint32_t dmem_read_uint(int* ok, uint32_t addr, int size) {
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

char* dmem_read_string(uint32_t addr) {
	uint32_t size = 0;
	uint8_t test_value;
	char* ret;
	/* 文字列の範囲を調べる(終端のNULを含む) */
	do {
		if (!dmemory_is_allocated(addr + size, 1)) return NULL;
		dmemory_read(&test_value, addr + size, 1);
		if (size == UINT32_MAX) return NULL;
		size++;
	} while (test_value != 0);
	/* 調べた範囲を読み込む */
	ret = malloc(size);
	if (ret == NULL) return NULL;
	dmemory_read(ret, addr, size);
	return ret;
}

int dmem_get_args(uint32_t esp, int num, ...) {
	va_list args;
	int ok = 1;
	int i;
	uint32_t addr = esp;
	va_start(args, num);
	for (i = 0; ok && i < num; i++) {
		uint32_t* dest = va_arg(args, uint32_t*);
		if (UINT32_MAX - addr < 4) {
			ok = 0;
			break;
		}
		addr += 4;
		*dest = dmem_read_uint(&ok, addr, 4);
	}
	va_end(args);
	return ok;
}
