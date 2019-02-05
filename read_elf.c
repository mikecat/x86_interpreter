#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "dynamic_memory.h"
#include "read_file.h"
#include "read_elf.h"

uint32_t read_num(const uint8_t* data, int size) {
	uint32_t ret = 0;
	int i;
	for (i = 0; i < size; i++) {
		ret |= data[i] << (i * 8);
	}
	return ret;
}

int read_elf(uint32_t* eip_value, const char* filename) {
	size_t filesize = 0;
	uint8_t* filedata = read_whole_file(&filesize, filename);
	uint32_t sh_offset, sh_ent_size, sh_num, sh_size;
	uint8_t* sheader;
	uint32_t i;
	if (filedata == 0) return 0;
	if (filesize < 16) {
		fprintf(stderr, "file size too small for ELF ident\n");
		free(filedata); return 0;
	}
	if (filedata[0] != 0x7f || filedata[1] != 'E' || filedata[2] != 'L' || filedata[3] != 'F') {
		fprintf(stderr, "not ELF file\n");
		free(filedata); return 0;
	}
	if (filedata[4] != 1) {
		fprintf(stderr, "only 32-bit ELF file is supported, but this is %s\n",
			filedata[4] == 0 ? "invalid" : filedata[4] == 2 ? "64-bit" : "unknown");
		free(filedata); return 0;
	}
	if (filedata[5] != 1) {
		fprintf(stderr, "only little endian ELF file is supported\n");
		free(filedata); return 0;
	}
	if (filedata[6] != 1) {
		fprintf(stderr, "warning: unknown ELF version %"PRIu8"\n", filedata[6]);
	}
	if (filesize < 52) {
		fprintf(stderr, "file size too small for ELF header\n");
		free(filedata); return 0;
	}
	*eip_value = read_num(filedata + 24, 4);
	sh_offset = read_num(filedata + 32, 4);
	sh_ent_size = read_num(filedata + 46, 2);
	sh_num = read_num(filedata + 48, 2);
	sh_size = (uint32_t)sh_ent_size * (uint32_t)sh_num;
	if (sh_offset == 0) {
		fprintf(stderr, "warning: no section header table in ELF\n");
		free(filedata); return 1;
	}
	if ((uint64_t)sh_offset + sh_size > filesize) {
		fprintf(stderr, "ELF section header table is out of file\n");
		free(filedata); return 0;
	}
	if (sh_ent_size < 40) {
		fprintf(stderr, "ELF section header entry size too small\n");
		free(filedata); return 0;
	}
	sheader = filedata + sh_offset;
	for (i = 0; i < sh_num; i++) {
		uint8_t* ent = sheader + sh_ent_size * i;
		uint32_t type = read_num(ent + 4, 4);
		uint32_t flags = read_num(ent + 8, 4);
		uint32_t addr = read_num(ent + 12, 4);
		uint32_t offset = read_num(ent + 16, 4);
		uint32_t size = read_num(ent + 20, 4);
		if (flags & 2) { /* メモリにロードする */
			if (size > 0 && UINT32_MAX - addr < size - 1) {
				fprintf(stderr, "ELF section %"PRIu32" out of address space\n", i);
				free(filedata); return 0;
			}
			dmemory_allocate(addr, size);
			if (type != 8) { /* SHT_NOBITSでない */
				if ((uint64_t)offset + size > filesize) {
					fprintf(stderr, "ELF section %"PRIu32" data is out of file\n", i);
					free(filedata); return 0;
				}
				dmemory_write(filedata + offset, addr, size);
			}
		}
	}
	free(filedata);
	return 1;
}
