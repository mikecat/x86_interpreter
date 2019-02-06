#include <stdio.h>
#include "pe_import.h"
#include "dynamic_memory.h"

int pe_import_initialize(const pe_import_params* params) {
	return 1;
}

int pe_import(uint32_t* eip, uint32_t regs[], uint32_t addr) {
	/* ret */
	dmemory_read(eip, regs[4], 4);
	regs[4] += 4;
	return 1;
}
