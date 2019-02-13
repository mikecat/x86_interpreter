#include <stdlib.h>
#include <inttypes.h>
#include "dmem_libc_stdlib.h"
#include "dynamic_memory.h"
#include "dmem_utils.h"

typedef struct heap_info_t {
	uint32_t size;
	int used;
	struct heap_info_t* next;
} heap_info_t;

static uint32_t heap_start;
static heap_info_t* heap_info_head;

int dmem_libc_stdlib_initialize(uint32_t heap_start_addr) {
	if (heap_start_addr % 64 != 0) {
		uint32_t delta = 64 - heap_start_addr % 64;
		if (UINT32_MAX - delta < heap_start_addr) return 0;
		heap_start = heap_start_addr + delta;
	} else {
		heap_start = heap_start_addr;
	}
	if (heap_start == 0) heap_start = 64;
	heap_info_head = NULL;
	return 1;
}

static uint32_t malloc_core(uint32_t size) {
	heap_info_t** pptr = &heap_info_head;
	heap_info_t* ptr = heap_info_head;
	heap_info_t* new_node;
	uint32_t addr = heap_start;
	if (UINT32_MAX - 63 < size) {
		return 0;
	}
	size = (size + UINT32_C(63)) / UINT32_C(64) * UINT32_C(64);
	if (size == 0) size = 64;
	while(ptr != NULL) {
		if (!ptr->used && ptr->size >= size) {
			/* 十分な空き領域が見つかった */
			if (ptr->size == size) {
				/* ちょうどいい大きさ */
				ptr->used = 1;
			} else {
				/* 余る */
				new_node = malloc(sizeof(heap_info_t));
				if (new_node == NULL) {
					return 0;
				}
				new_node->size = ptr->size - size;
				new_node->used = 0;
				new_node->next = ptr->next;
				ptr->size = size;
				ptr->used = 1;
				ptr->next = new_node;
			}
			return addr;
		} else {
			addr += ptr->size;
			pptr = &ptr->next;
			ptr = ptr->next;
		}
	}
	/* 空き領域が見つからなかったので、作る */
	new_node = malloc(sizeof(heap_info_t));
	if (new_node == NULL) {
		return 0;
	}
	dmemory_allocate(addr, size);
	new_node->size = size;
	new_node->used = 1;
	new_node->next = NULL;
	*pptr = new_node;
	return addr;
}

static int free_core(uint32_t addr_to_free) {
	heap_info_t* prev_ptr = NULL;
	heap_info_t* ptr = heap_info_head;
	uint32_t addr = heap_start;
	if (addr_to_free == 0) return 1;
	while (ptr != NULL) {
		if (ptr->used && addr == addr_to_free) {
			ptr->used = 0;
			if (prev_ptr != NULL && !prev_ptr->used) {
				/* 空き領域を統合する */
				prev_ptr->size += ptr->size;
				prev_ptr->next = ptr->next;
				free(ptr);
			}
			return 1;
		}
		addr += ptr->size;
		prev_ptr = ptr;
		ptr = ptr->next;
	}
	/* 該当の領域が見つからなかった */
	return 0;
}

int dmem_libc_free(uint32_t* ret, uint32_t esp) {
	uint32_t addr_to_free;
	(void)ret; /* free()は戻り値がvoidなので、戻り値を更新しない */
	if (!dmem_get_args(esp, 1, &addr_to_free)) return 0;
	return free_core(addr_to_free);
}

int dmem_libc_malloc(uint32_t* ret, uint32_t esp) {
	uint32_t size;
	if (!dmem_get_args(esp, 1, &size)) return 0;
	*ret = malloc_core(size);
	return 1;
}

int dmem_libc_realloc(uint32_t* ret, uint32_t esp) {
	uint32_t old_addr, new_size;
	heap_info_t* ptr = heap_info_head;
	uint32_t addr = heap_start;
	int printf(const char*,...);
	if (!dmem_get_args(esp, 2, &old_addr, &new_size)) return 0;
	if (old_addr == 0) {
		*ret = malloc_core(new_size);
		return 1;
	}
	if (UINT32_MAX - 63 < new_size) {
		return 0;
	}
	new_size = (new_size + UINT32_C(63)) / UINT32_C(64) * UINT32_C(64);
	if (new_size == 0) new_size = 64;
	while (ptr != NULL) {
		if (ptr->used && addr == old_addr) {
			if (ptr->size == new_size) {
				/* そのまま */
				*ret = addr;
			} else if (new_size < ptr->size) {
				/* 領域を減らす */
				uint32_t delta = ptr->size - new_size;
				if (ptr->next != NULL && !ptr->next->used) {
					if (UINT32_MAX - delta < ptr->next->size) return 0;
					ptr->next->size += delta;
				} else {
					heap_info_t* new_node = malloc(sizeof(heap_info_t));
					if (new_node == NULL) return 0;
					new_node->size = delta;
					new_node->used = 0;
					new_node->next = ptr->next;
					ptr->next = new_node;
				}
				ptr->size = new_size;
				*ret = addr;
			} else {
				/* 領域を増やす */
				uint32_t delta = new_size - ptr->size;
				if (ptr->next != NULL && !ptr->next->used && ptr->next->size <= delta) {
					/* 今の領域を再利用して増やせる余裕がある */
					if (ptr->next->size == delta) {
						heap_info_t* next_node = ptr->next;
						ptr->next = ptr->next->next;
						free(next_node);
					} else {
						ptr->next->size -= delta;
					}
					ptr->size = new_size;
					*ret = addr;
				} else {
					/* 余裕が無い */
					uint32_t old_size = ptr->size;
					char* buf = malloc(old_size);
					if (buf == NULL) {
						*ret = 0;
					} else {
						uint32_t new_addr;
						dmemory_read(buf, addr, old_size);
						if (!free_core(addr)) {
							free(buf);
							return 0;
						}
						new_addr = malloc_core(new_size);
						if (new_addr != 0) dmemory_write(buf, new_addr, old_size);
						free(buf);
						*ret = new_addr;
					}
				}
			}
			return 1;
		}
	}
	/* 指定の領域が見つからなかった */
	return 0;
}
