#include <stdio.h>
#include <stdint.h>
#include "dynamic_memory.h"

#define BUFFER_SIZE 4096

int read_raw(const char* filename) {
	FILE* fp;
	uint32_t read_addr = 0;
	int of_flag = 0;
	fp = fopen(filename, "rb");
	if (fp == NULL) {
		fprintf(stderr, "input file open failed\n");
		return 0;
	}
	for (;;) {
		uint8_t data[BUFFER_SIZE];
		size_t readSize = fread(data, sizeof(*data), BUFFER_SIZE, fp);
		if (ferror(fp)) {
			fprintf(stderr, "input file read error\n");
			fclose(fp);
			return 0;
		} else if (readSize == 0) {
			break;
		} else {
			if (of_flag || UINT32_MAX - read_addr < readSize - 1) {
				fprintf(stderr, "input file too big\n");
				fclose(fp);
				return 0;
			}
			dmemory_allocate(read_addr, readSize);
			dmemory_write(data, read_addr, readSize);
			if (UINT32_MAX - read_addr < readSize) of_flag = 1;
			read_addr += readSize;
		}
	}
	fclose(fp);
	return 1;
}
