#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dynamic_memory.h"

#define FIRST_TABLE_SIZE 1024
#define FIRST_TABLE_SHIFT 22
#define SECOND_TABLE_SIZE 1024
#define SECOND_TABLE_SHIFT 12
#define ALLOCATE_UNIT_SIZE 4096

typedef uint8_t allocate_unit[ALLOCATE_UNIT_SIZE];
typedef allocate_unit* allocate_unit_table[SECOND_TABLE_SIZE];

static allocate_unit_table* aut_table[FIRST_TABLE_SIZE];

static int get_idxs(int* fidx_s, int* sidx_s, int* fidx_e, int* sidx_e, uint32_t addr, uint32_t size) {
	if (size == 0) return 0;
	size--;
	if (size > UINT32_MAX - addr) return 0;
	*fidx_s = (addr >> FIRST_TABLE_SHIFT) % FIRST_TABLE_SIZE;
	*sidx_s = (addr >> SECOND_TABLE_SHIFT) % SECOND_TABLE_SIZE;
	*fidx_e = ((addr + size) >> FIRST_TABLE_SHIFT) % FIRST_TABLE_SIZE;
	*sidx_e = ((addr + size) >> SECOND_TABLE_SHIFT) % SECOND_TABLE_SIZE;
	return 1;
}

void dmemory_read(void* dest, uint32_t addr, uint32_t size) {
	if (size > 0 && size - 1 > UINT32_MAX - addr) size = UINT32_MAX - addr + 1;
	uint8_t* destu8 = (uint8_t*)dest;
	int fidx_s, sidx_s, fidx_e, sidx_e;
	if (!get_idxs(&fidx_s, &sidx_s, &fidx_e, &sidx_e, addr, size)) return;
	uint32_t read_offset = addr % ALLOCATE_UNIT_SIZE;
	uint32_t read_size = ALLOCATE_UNIT_SIZE - read_offset;
	int i, j;
	for (i = fidx_s; i <= fidx_e; i++) {
		int jmin = (i == fidx_s ? sidx_s : 0);
		int jmax = (i == fidx_e ? sidx_e : SECOND_TABLE_SIZE - 1);
		for (j = jmin; j <= jmax; j++) {
			if (read_size > size) read_size = size;
			if (aut_table[i] != NULL && (*aut_table[i])[j] != NULL) {
				memcpy(destu8, *(*aut_table[i])[j] + read_offset, read_size);
			}
			destu8 += read_size;
			size -= read_size;
			read_offset = 0;
			read_size = ALLOCATE_UNIT_SIZE;
		}
	}
}

void dmemory_write(void* src, uint32_t addr, uint32_t size) {
	if (size > 0 && size - 1 > UINT32_MAX - addr) size = UINT32_MAX - addr + 1;
	uint8_t* srcu8 = (uint8_t*)src;
	int fidx_s, sidx_s, fidx_e, sidx_e;
	if (!get_idxs(&fidx_s, &sidx_s, &fidx_e, &sidx_e, addr, size)) return;
	uint32_t write_offset = addr % ALLOCATE_UNIT_SIZE;
	uint32_t write_size = ALLOCATE_UNIT_SIZE - write_offset;
	int i, j;
	for (i = fidx_s; i <= fidx_e; i++) {
		int jmin = (i == fidx_s ? sidx_s : 0);
		int jmax = (i == fidx_e ? sidx_e : SECOND_TABLE_SIZE - 1);
		for (j = jmin; j <= jmax; j++) {
			if (write_size > size) write_size = size;
			if (aut_table[i] != NULL && (*aut_table[i])[j] != NULL) {
				memcpy(*(*aut_table[i])[j] + write_offset, srcu8, write_size);
			}
			srcu8 += write_size;
			size -= write_size;
			write_offset = 0;
			write_size = ALLOCATE_UNIT_SIZE;
		}
	}
}

void dmemory_allocate(uint32_t addr, uint32_t size) {
	int fidx_s, sidx_s, fidx_e, sidx_e;
	if (!get_idxs(&fidx_s, &sidx_s, &fidx_e, &sidx_e, addr, size)) return;
	int i, j;
	for (i = fidx_s; i <= fidx_e; i++) {
		int jmin = (i == fidx_s ? sidx_s : 0);
		int jmax = (i == fidx_e ? sidx_e : SECOND_TABLE_SIZE - 1);
		if (aut_table[i] == NULL) {
			aut_table[i] = malloc(sizeof(*aut_table[i]));
			if (aut_table[i] == NULL) {
				perror("malloc");
				exit(1);
			}
			for (j = 0; j < SECOND_TABLE_SIZE; j++) (*aut_table[i])[j] = NULL;
		}
		for (j = jmin; j <= jmax; j++) {
			if ((*aut_table[i])[j] == NULL) {
				(*aut_table[i])[j] = calloc(1, sizeof(*(*aut_table[i])[j]));
				if ((*aut_table[i])[j] == NULL) {
					perror("calloc");
					exit(1);
				}
			}
		}
	}
}

void dmemory_deallocate(uint32_t addr, uint32_t size) {
	int fidx_s, sidx_s, fidx_e, sidx_e;
	if (!get_idxs(&fidx_s, &sidx_s, &fidx_e, &sidx_e, addr, size)) return;
	int i, j;
	for (i = fidx_s; i <= fidx_e; i++) {
		int jmin = (i == fidx_s ? sidx_s : 0);
		int jmax = (i == fidx_e ? sidx_e : SECOND_TABLE_SIZE - 1);
		if (aut_table[i] != NULL) {
			for (j = jmin; j <= jmax; j++) {
				free((*aut_table[i])[j]);
				(*aut_table[i])[j] = NULL;
			}
			if (jmin == 0 && jmax == SECOND_TABLE_SIZE - 1) {
				free(aut_table[i]);
				aut_table[i] = NULL;
			}
		}
	}
}

int dmemory_is_allocated(uint32_t addr, uint32_t size) {
	int fidx_s, sidx_s, fidx_e, sidx_e;
	if (size == 0) return 1;
	if (!get_idxs(&fidx_s, &sidx_s, &fidx_e, &sidx_e, addr, size)) return 0;
	int i, j;
	for (i = fidx_s; i <= fidx_e; i++) {
		int jmin = (i == fidx_s ? sidx_s : 0);
		int jmax = (i == fidx_e ? sidx_e : SECOND_TABLE_SIZE - 1);
		for (j = jmin; j <= jmax; j++) {
			if (aut_table[i] == NULL || (*aut_table[i])[j] == NULL) return 0;
		}
	}
	return 1;
}
