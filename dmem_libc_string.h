#ifndef DMEM_LIBC_STRING_H_GUARD_A7FB8126_EA21_4C34_9F95_CD6359546FBE
#define DMEM_LIBC_STRING_H_GUARD_A7FB8126_EA21_4C34_9F95_CD6359546FBE

#include <stdint.h>

int dmem_libc_string_initialize(void);

int dmem_libc_memcpy(uint32_t* ret, uint32_t esp);
int dmem_libc_strcpy(uint32_t* ret, uint32_t esp);
int dmem_libc_strncpy(uint32_t* ret, uint32_t esp);
int dmem_libc_strcmp(uint32_t* ret, uint32_t esp);
int dmem_libc_strncmp(uint32_t* ret, uint32_t esp);
int dmem_libc_strchr(uint32_t* ret, uint32_t esp);
int dmem_libc_memset(uint32_t* ret, uint32_t esp);
int dmem_libc_strlen(uint32_t* ret, uint32_t esp);

#endif
