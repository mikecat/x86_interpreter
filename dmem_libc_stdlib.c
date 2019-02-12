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
	heap_start = heap_start_addr;
	heap_info_head = NULL;
	return 1;
}

int dmem_libc_malloc(uint32_t* ret, uint32_t esp) {
	uint32_t size;
	heap_info_t** pptr = &heap_info_head;
	heap_info_t* ptr = heap_info_head;
	heap_info_t* new_node;
	uint32_t addr = heap_start;
	if (!dmem_get_args(esp, 1, &size)) return 0;
	if (size == 0 || UINT32_MAX - 63 < size) {
		*ret = 0;
		return 1;
	}
	size = (size + UINT32_C(63)) / UINT32_C(64) * UINT32_C(64);
	while(ptr != NULL) {
		if (!ptr->used && ptr->size >= size) {
			/* 十分な空き領域が見つかった */
			if (ptr->size == size) {
				/* ちょうどいい大きさ */
				ptr->used = 1;
			} else {
				/* 余る */
				new_node = malloc(sizeof(heap_info_t));
				if (new_node == 0) {
					*ret = 0;
					return 1;
				}
				new_node->size = ptr->size - size;
				new_node->used = 0;
				new_node->next = ptr->next;
				ptr->size = size;
				ptr->used = 1;
				ptr->next = new_node;
			}
			*ret = addr;
			return 1;
		} else {
			addr += ptr->size;
			pptr = &ptr->next;
			ptr = ptr->next;
		}
	}
	/* 空き領域が見つからなかったので、作る */
	dmemory_allocate(addr, size);
	new_node = malloc(sizeof(heap_info_t));
	if (new_node == 0) {
		*ret = 0;
		return 1;
	}
	new_node->size = size;
	new_node->used = 1;
	new_node->next = NULL;
	*pptr = new_node;
	*ret = addr;
	return 1;
}