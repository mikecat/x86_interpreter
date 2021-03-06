#ifndef DMEM_LIBC_STDIO_H_GUARD_3294DE58_BD9B_412C_A988_D2950B9E8913
#define DMEM_LIBC_STDIO_H_GUARD_3294DE58_BD9B_412C_A988_D2950B9E8913

#include <stdint.h>

int dmem_libc_stdio_initialize(uint32_t iob_addr_in);

int dmem_libc_fclose(uint32_t* ret, uint32_t esp);
int dmem_libc_fflush(uint32_t* ret, uint32_t esp);
int dmem_libc_fopen(uint32_t* ret, uint32_t esp);
int dmem_libc_fprintf(uint32_t* ret, uint32_t esp);
int dmem_libc_printf(uint32_t* ret, uint32_t esp);
int dmem_libc_sprintf(uint32_t* ret, uint32_t esp);
int dmem_libc_vfprintf(uint32_t* ret, uint32_t esp);
int dmem_libc_fputs(uint32_t* ret, uint32_t esp);
int dmem_libc_puts(uint32_t* ret, uint32_t esp);
int dmem_libc_fread(uint32_t* ret, uint32_t esp);
int dmem_libc_fwrite(uint32_t* ret, uint32_t esp);

/* ファイルを扱う都合上、ここでやる */
int dmem_flsbuf(uint32_t* ret, uint32_t esp);
int dmem_filbuf(uint32_t* ret, uint32_t esp);
int dmem_read(uint32_t* ret, uint32_t esp);

#endif
