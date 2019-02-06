#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "dynamic_memory.h"
#include "read_file.h"
#include "read_pe.h"

static uint32_t read_num(const uint8_t* data, int size) {
	uint32_t ret = 0;
	int i;
	for (i = 0; i < size; i++) {
		ret |= data[i] << (i * 8);
	}
	return ret;
}

int read_pe(uint32_t* eip_value, uint32_t* stack_size, pe_import_params* import_params, const char* filename) {
	size_t filesize = 0;
	uint8_t* filedata = read_whole_file(&filesize, filename);
	uint32_t newheader_offset;
	uint8_t* newheader;
	uint32_t num_section, optheader_size, section_table_size;
	uint8_t* optheader;
	uint32_t magic, entrypoint, image_base, stack_reserve;
	uint8_t* section_table;
	uint32_t i;
	if (filedata == 0) return 0;
	if (filesize < 64) {
		fprintf(stderr, "file size too small for PE header\n");
		free(filedata); return 0;
	}
	if (filedata[0] != 'M' || filedata[1] != 'Z') {
		fprintf(stderr, "not PE file\n");
		free(filedata); return 0;
	}
	newheader_offset = read_num(filedata + 60, 4);
	if (newheader_offset > filesize - 24) {
		fprintf(stderr, "file size too small for PE new header\n");
		free(filedata); return 0;
	}
	newheader = filedata + newheader_offset;
	if (newheader[0] != 'P' || newheader[1] != 'E' || newheader[2] != 0x00 || newheader[3] != 0x00) {
		fprintf(stderr, "wrong PE new header signature\n");
		free(filedata); return 0;
	}
	num_section = read_num(newheader + 6, 2);
	optheader_size = read_num(newheader + 20, 2);
	section_table_size = 40 * num_section;
	if (optheader_size > filesize || filesize - optheader_size < newheader_offset + 24) {
		fprintf(stderr, "file size too small for PE optional header\n");
		free(filedata); return 0;
	}
	if (optheader_size < 76) { /* TODO */
		fprintf(stderr, "PE optional header too small\n");
		free(filedata); return 0;
	}
	optheader = newheader + 24;
	magic = read_num(optheader + 0, 2);
	entrypoint = read_num(optheader + 16, 4);
	image_base = read_num(optheader + 28, 4);
	stack_reserve = read_num(optheader + 72, 4);
	if (magic == 0x20b) {
		fprintf(stderr, "unsupported 64-bit PE file detected\n");
		free(filedata); return 0;
	}
	if (UINT32_MAX - image_base < entrypoint) {
		fprintf(stderr, "PE entrypoint out of address space\n");
		free(filedata); return 0;
	}
	if (import_params != NULL) {
		uint32_t import_addr, import_size, iat_addr, iat_size;
		if (optheader_size >= 112) {
			import_addr = read_num(optheader + 104, 4);
			import_size = read_num(optheader + 108, 4);
		} else {
			import_addr = 0;
			import_size = 0;
		}
		if (optheader_size >= 200) {
			iat_addr = read_num(optheader + 192, 4);
			iat_size = read_num(optheader + 196, 4);
		} else {
			iat_addr = 0;
			iat_size = 0;
		}
		if (import_size > 0) {
			if (UINT32_MAX - image_base < import_addr ||
			UINT32_MAX - (image_base + import_addr) < import_size - 1) {
				fprintf(stderr, "PE import informaton out of address space\n");
				free(filedata); return 0;
			}
		}
		if (iat_size > 0) {
			if (UINT32_MAX - image_base < iat_addr ||
			UINT32_MAX - (image_base + iat_addr) < iat_size - 1) {
				fprintf(stderr, "PE import address table out of address space\n");
				free(filedata); return 0;
			}
		}
		import_params->image_base = image_base;
		import_params->import_addr = image_base + import_addr;
		import_params->import_size = import_size;
		import_params->iat_addr = image_base + iat_addr;
		import_params->iat_size = iat_size;
	}
	if (section_table_size > filesize || filesize - section_table_size < newheader_offset + 24 + optheader_size) {
		fprintf(stderr, "file size too small for PE section table\n");
		free(filedata); return 0;
	}
	section_table = optheader + optheader_size;
	for (i = 0; i < num_section; i++) {
		uint8_t* section_data = section_table + 40 * i;
		uint32_t size, addr, file_size, file_offset;
		uint32_t load_size;
		size = read_num(section_data + 8, 4);
		addr = read_num(section_data + 12, 4);
		file_size = read_num(section_data + 16, 4);
		file_offset = read_num(section_data + 20, 4);
		load_size = (file_offset == 0 ? 0 : (size < file_size ? size : file_size));
		if (UINT32_MAX - image_base < addr || (size > 0 && UINT32_MAX - (image_base + addr) < size - 1)) {
			fprintf(stderr, "PE section %"PRIu32" out of address space\n", i);
			free(filedata); return 0;
		}
		if (filesize - file_offset < load_size) {
			fprintf(stderr, "PE section %"PRIu32" out of file\n", i);
			free(filedata); return 0;
		}
		dmemory_allocate(image_base + addr, size);
		dmemory_write(filedata + file_offset, image_base + addr, load_size);
	}
	if (eip_value != NULL) *eip_value = image_base + entrypoint;
	if (stack_size != NULL) *stack_size = stack_reserve;
	free(filedata);
	return 1;
}
