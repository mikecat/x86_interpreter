#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "dynamic_memory.h"
#include "read_file.h"

int read_raw(const char* filename) {
	size_t file_size = 0;
	void* file_data = read_whole_file(&file_size, filename);
	if (file_data == NULL) return 0;
	dmemory_allocate(0, file_size);
	dmemory_write(file_data, 0, file_size);
	free(file_data);
	return 1;
}
