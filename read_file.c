#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "read_file.h"

#define BUFFER_SIZE 4096

void* read_whole_file(size_t* size, const char* filename) {
	FILE* fp;
	size_t read_size = 0;
	char* buffer = NULL;
	fp = fopen(filename, "rb");
	if (fp == NULL) {
		fprintf(stderr, "input file open failed\n");
		return NULL;
	}
	for (;;) {
		char data[BUFFER_SIZE];
		size_t new_read_size = fread(data, sizeof(*data), BUFFER_SIZE, fp);
		if (ferror(fp)) {
			fprintf(stderr, "input file read error\n");
			fclose(fp);
			free(buffer);
			return NULL;
		} else if (new_read_size == 0) {
			break;
		} else {
			char *new_buffer;
			if (SIZE_MAX - read_size < new_read_size) {
				fprintf(stderr, "input file too big\n");
				fclose(fp);
				free(buffer);
				return NULL;
			}
			new_buffer = realloc(buffer, read_size + new_read_size);
			if (new_buffer == NULL) {
				perror("realloc");
				free(buffer);
				fclose(fp);
				return NULL;
			}
			buffer = new_buffer;
			memcpy(buffer + read_size, data, new_read_size);
			read_size += new_read_size;
		}
	}
	fclose(fp);
	if (size != NULL) *size = read_size;
	return buffer;
}
