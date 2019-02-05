#include <stdio.h>
#include <stdlib.h>
#include "dynamic_memory.h"
#include "xv6_syscall.h"

uint32_t readint(int* ok, uint32_t addr, uint32_t size) {
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

int xv6_syscall(uint32_t regs[]) {
	uint32_t esp = regs[4];
	switch (regs[0]) {
		case 2: /* exit */
			return 0;
		case 5: /* read */
			{
				uint32_t fd, buf, n;
				int ok1 = 0, ok2 = 0, ok3 = 0;
				FILE* fp;
				uint8_t* data;
				size_t read_size;
				fd = readint(&ok1, esp + 4, 4);
				buf = readint(&ok2, esp + 8, 4);
				n = readint(&ok3, esp + 12, 4);
				if (!(ok1 && ok2 && ok3)) {
					regs[0] = -1;
					break;
				}
				/* 入力元の指定(暫定対応) */
				if (fd == 0) {
					fp = stdin;
				} else {
					regs[0] = -1;
					break;
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
					regs[0] = -1;
					break;
				}
				dmemory_write(data, buf, read_size);
				free(data);
				/* 成功 */
				regs[0] = read_size;
			}
			break;
		case 16: /* write */
			{
				uint32_t fd, buf, n;
				int ok1 = 0, ok2 = 0, ok3 = 0;
				FILE* fp;
				uint8_t* data;
				fd = readint(&ok1, esp + 4, 4);
				buf = readint(&ok2, esp + 8, 4);
				n = readint(&ok3, esp + 12, 4);
				if (!(ok1 && ok2 && ok3)) {
					regs[0] = -1;
					break;
				}
				if (!dmemory_is_allocated(buf, n)) {
					/* 指定された領域が確保されていない */
					regs[0] = -1;
					break;
				}
				/* 出力先の指定(暫定対応) */
				if (fd == 1) {
					fp = stdout;
				} else if (fd == 2) {
					fp = stderr;
				} else {
					regs[0] = -1;
					break;
				}
				/* データを取得して出力 */
				data = malloc(n);
				if (data == NULL) {
					perror("malloc");
					return 0;
				}
				dmemory_read(data, buf, n);
				if (fwrite(data, 1, n, fp) != n) {
					regs[0] = -1;
					break;
				}
				free(data);
				/* 成功 */
				regs[0] = n;
			}
			break;
		default: /* 不正もしくは未実装 */
			regs[0] = -1;
			break;
	}
	return 1;
}
