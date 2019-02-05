#include <stdio.h>
#include "xv6_syscall.h"

int xv6_syscall(uint32_t regs[]) {
	switch (regs[0]) {
		default: /* 不正もしくは未実装 */
			regs[0] = -1;
			break;
	}
	return 1;
}
