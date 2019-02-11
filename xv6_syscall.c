#include <stdio.h>
#include <stdlib.h>
#include "x86_regs.h"
#include "dynamic_memory.h"
#include "xv6_syscall.h"

static int enable_sbrk = 0;
static uint32_t sbrk_origin = 0;
static uint32_t sbrk_addr = 0;

void initialize_xv6_sbrk(uint32_t initial_addr) {
	enable_sbrk = 1;
	sbrk_origin = initial_addr;
	sbrk_addr = initial_addr;
}

static uint32_t readint(int* ok, uint32_t addr, uint32_t size) {
	uint8_t buffer[4];
	uint32_t result = 0;
	uint32_t i;
	if (size > 4) {
		*ok = 0;
		return 0;
	}
	if (!dmemory_is_allocated(addr, size)) {
		*ok = 0;
		return 0;
	}
	dmemory_read(buffer, addr, size);
	for (i = 0; i < size; i++) {
		result |= buffer[i] << (8 * i);
	}
	*ok = 1;
	return result;
}

static int xv6_read(uint32_t regs[]) {
	uint32_t fd, buf, n;
	int ok1 = 0, ok2 = 0, ok3 = 0;
	FILE* fp;
	uint8_t* data;
	size_t read_size;
	fd = readint(&ok1, regs[ESP] + 4, 4);
	buf = readint(&ok2, regs[ESP] + 8, 4);
	n = readint(&ok3, regs[ESP] + 12, 4);
	if (!(ok1 && ok2 && ok3)) {
		regs[EAX] = -1;
		return 1;
	}
	/* 入力元の指定(暫定対応) */
	if (fd == 0) {
		fp = stdin;
	} else {
		regs[0] = -1;
		return 1;
	}
	/* データを取得 */
	data = malloc(n);
	if (data == NULL) {
		perror("malloc");
		return 0;
	}
	read_size = fread(data, 1, n, fp);
	if (!dmemory_is_allocated(buf, read_size)) {
		/* 指定された領域が確保されていない */
		regs[EAX] = -1;
		return 1;
	}
	dmemory_write(data, buf, read_size);
	free(data);
	/* 成功 */
	regs[EAX] = read_size;
	return 1;
}

static int xv6_sbrk(uint32_t regs[]) {
	uint32_t n;
	uint32_t new_addr;
	int ok;
	n = readint(&ok, regs[ESP] + 4, 4);
	if (!ok) {
		regs[EAX] = -1;
		return 1;
	}
	new_addr = sbrk_addr + n;
	if (n & UINT32_C(0x80000000) ? new_addr > sbrk_addr : new_addr < sbrk_addr) {
		/* オーバーフロー */
		regs[EAX] = -1;
		return 1;
	}
	if (new_addr < sbrk_origin) {
		/* 減らしすぎ(本家ではOK?) */
		regs[EAX] = -1;
		return 1;
	}
	if (sbrk_addr < new_addr) {
		dmemory_allocate(sbrk_addr, new_addr - sbrk_addr);
	}
	/* 成功 */
	regs[EAX] = sbrk_addr;
	sbrk_addr = new_addr;
	return 1;
}

static int xv6_write(uint32_t regs[]) {
	uint32_t fd, buf, n;
	int ok1 = 0, ok2 = 0, ok3 = 0;
	FILE* fp;
	uint8_t* data;
	fd = readint(&ok1, regs[ESP] + 4, 4);
	buf = readint(&ok2, regs[ESP] + 8, 4);
	n = readint(&ok3, regs[ESP] + 12, 4);
	if (!(ok1 && ok2 && ok3)) {
		regs[EAX] = -1;
		return 1;
	}
	if (!dmemory_is_allocated(buf, n)) {
		/* 指定された領域が確保されていない */
		regs[EAX] = -1;
		return 1;
	}
	/* 出力先の指定(暫定対応) */
	if (fd == 1) {
		fp = stdout;
	} else if (fd == 2) {
		fp = stderr;
	} else {
		regs[EAX] = -1;
		return 1;
	}
	/* データを取得して出力 */
	data = malloc(n);
	if (data == NULL) {
		perror("malloc");
		return 0;
	}
	dmemory_read(data, buf, n);
	if (fwrite(data, 1, n, fp) != n) {
		regs[EAX] = -1;
		return 1;
	}
	free(data);
	/* 成功 */
	regs[EAX] = n;
	return 1;
}

int xv6_syscall(uint32_t regs[]) {
	switch (regs[EAX]) {
		case 2: /* exit */
			return 0;
		case 5: /* read */
			return xv6_read(regs);
		case 12: /* sbrk */
			if (!enable_sbrk) {
				regs[EAX] = -1;
				return 1;
			} else {
				return xv6_sbrk(regs);
			}
		case 16: /* write */
			return xv6_write(regs);
		default: /* 不正もしくは未実装 */
			regs[EAX] = -1;
			break;
	}
	return 1;
}
