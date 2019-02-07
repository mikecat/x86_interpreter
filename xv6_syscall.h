#ifndef XV6_SYSCALL_H_GUARD_05AC6687_373D_412B_8406_6BA9C13F8576
#define XV6_SYSCALL_H_GUARD_05AC6687_373D_412B_8406_6BA9C13F8576

#include <stdint.h>

void initialize_xv6_sbrk(uint32_t initial_addr);

/* 成功:1 失敗:-1 プログラム終了(成功):0 */
int xv6_syscall(uint32_t regs[]);

#endif
