#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "x86_regs.h"
#include "pe_import.h"
#include "dynamic_memory.h"

typedef struct {
	int is_ord;
	char* name;
	uint16_t hint_or_ord;
} func_info;

typedef struct {
	uint32_t int_addr, iat_addr, iat_size;
	int has_int;
	char* name;
	func_info* funcs;
	uint32_t func_num;
} imported_lib_info;

static imported_lib_info* imported_libs = NULL;
static uint32_t imported_lib_count = 0;

static uint32_t read_num(const uint8_t* data, int size) {
	uint32_t ret = 0;
	int i;
	for (i = 0; i < size; i++) {
		ret |= data[i] << (i * 8);
	}
	return ret;
}

static char* read_string_dmem(uint32_t addr) {
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

int pe_import_initialize(const pe_import_params* params) {
	uint32_t iat_end = params->iat_addr + (params->iat_size > 0 ? params->iat_size - 1 : 0);
	free(imported_libs);
	imported_libs = NULL;
	imported_lib_count = 0;
	if (params->import_size >= 20) {
		uint32_t i;
		for (i = 0; i * 20 <= params->import_size - 20; i++) {
			uint32_t this_addr = params->import_addr + i * 20;
			uint8_t data[20];
			uint32_t name_ptr;
			uint32_t j;
			int all_zero = 1;
			imported_lib_info* new_libs;
			/* 情報をロードする */
			if (!dmemory_is_allocated(this_addr, 20)) {
				fprintf(stderr, "PE import descriptor %"PRIu32" not allocated!\n", i);
				return 0;
			}
			dmemory_read(data, this_addr, 20);
			for (j = 0; j < 20; j++) {
				if (data[j] != 0) all_zero = 0;
			}
			if (all_zero) break;
			new_libs = realloc(imported_libs, sizeof(*imported_libs) * (i + 1));
			if (new_libs == NULL) {
				perror("realloc");
				return 0;
			}
			/* 情報をデコードする */
			imported_libs = new_libs;
			imported_libs[i].int_addr = read_num(data + 0, 4);
			imported_libs[i].iat_addr = read_num(data + 16, 4);
			name_ptr = read_num(data + 12, 4);
			if (UINT32_MAX - params->image_base < imported_libs[i].int_addr ||
			UINT32_MAX - params->image_base < imported_libs[i].iat_addr ||
			UINT32_MAX - params->image_base < name_ptr) {
				fprintf(stderr, "some address in PE import descriptor %"PRIu32" out of address space!\n", i);
				return 0;
			}
			imported_libs[i].has_int = (imported_libs[i].int_addr != 0);
			imported_libs[i].int_addr += params->image_base;
			imported_libs[i].iat_addr += params->image_base;
			imported_libs[i].iat_size = 0;
			name_ptr += params->image_base;
			/* インポート対象の名前情報を得る */
			imported_libs[i].name = read_string_dmem(name_ptr);
			if (imported_libs[i].name == NULL) {
				fprintf(stderr, "name in PE import descriptor %"PRIu32" is invalid!\n", i);
				return 0;
			}
			/* INT/IATから情報を得る */
			if (!imported_libs[i].has_int) imported_libs[i].int_addr = imported_libs[i].iat_addr;
			imported_libs[i].funcs = NULL;
			imported_libs[i].func_num = 0;
			if (iat_end >= 3) {
				for (j = 0; imported_libs[i].iat_addr + j * 4 <= iat_end - 3; j++) {
					uint8_t entry[4];
					uint32_t entry_value;
					func_info* new_funcs;
					func_info* new_func;
					if (UINT32_MAX - imported_libs[i].int_addr < j * 4 ||
					!dmemory_is_allocated(imported_libs[i].int_addr + j * 4, 4)) {
						fprintf(stderr, "unallocated entry exists in INT/IAT of PE import descriptor %"PRIu32"\n", i);
						return 0;
					}
					dmemory_read(entry, imported_libs[i].int_addr + j * 4, 4);
					entry_value = read_num(entry, 4);
					if (entry_value == 0) break;
					new_funcs = realloc(imported_libs[i].funcs, sizeof(*new_funcs) * (j + 1));
					if (new_funcs == NULL) {
						perror("realloc");
						return 0;
					}
					imported_libs[i].funcs = new_funcs;
					new_func = &imported_libs[i].funcs[j];
					if (entry_value & UINT32_C(0x80000000)) {
						new_func->is_ord = 1;
						new_func->name = NULL;
						new_func->hint_or_ord = entry_value & 0xffff;
					} else {
						uint8_t hint[2];
						if (UINT32_MAX - params->image_base < entry_value + 2 ||
						!dmemory_is_allocated(params->image_base + entry_value, 2) ||
						(new_func->name = read_string_dmem(params->image_base + entry_value + 2)) == NULL) {
							fprintf(stderr, "invalid entry exists in INT/IAT of PE import descriptor %"PRIu32"\n", i);
							return 0;
						}
						dmemory_read(hint, params->image_base + entry_value, 2);
						new_func->is_ord = 0;
						new_func->hint_or_ord = read_num(hint, 2);
					}
				}
				imported_libs[i].func_num = j;
				imported_libs[i].iat_size = j * 4;
			}
		}
		imported_lib_count = i;
	}
	return 1;
}

int pe_import(uint32_t* eip, uint32_t regs[], uint32_t addr) {
	uint32_t i;
	const imported_lib_info* called_lib = NULL;
	const func_info* called_func = NULL;
	for (i = 0; i < imported_lib_count; i++) {
		if (imported_libs[i].iat_addr <= addr && addr - imported_libs[i].iat_addr < imported_libs[i].iat_size) {
			uint32_t offset = addr - imported_libs[i].iat_addr;
			called_lib = &imported_libs[i];
			if (offset % 4 == 0) {
				called_func = &imported_libs[i].funcs[offset / 4];
			}
			break;
		}
	}
	if (called_func != NULL) {
		printf("function ");
		if (called_func->is_ord) {
			printf("#%"PRIu16, called_func->hint_or_ord);
		} else {
			printf("%s()", called_func->name);
		}
		printf(" in library %s is called.\n", called_lib->name);
	}
	/* ret */
	dmemory_read(eip, regs[ESP], 4);
	regs[ESP] += 4;
	return 1;
}
