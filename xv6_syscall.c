#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "x86_regs.h"
#include "dynamic_memory.h"
#include "xv6_syscall.h"

static uint32_t sbrk_origin = 0;
static uint32_t sbrk_addr = 0;

typedef struct {
	FILE* stream;
	int ref_cnt;
	int can_read, can_write;
	enum e_pop {
		POP_NONE, POP_READ, POP_WRITE, POP_SEEK
	} prev_operation;
} stream_info;

#define STREAM_MAX 1024
#define FD_MAX 1024

static stream_info streams[STREAM_MAX];
static stream_info* fds[FD_MAX];

int initialize_xv6_syscall(uint32_t work_addr) {
	int i;
	sbrk_origin = work_addr;
	sbrk_addr = work_addr;

	for (i = 0; i < STREAM_MAX; i++) {
		streams[i].stream = NULL;
		streams[i].ref_cnt = 0;
		streams[i].can_read = 0;
		streams[i].can_write = 0;
		streams[i].prev_operation = POP_NONE;
	}
	for (i = 0; i < FD_MAX; i++) {
		fds[i] = NULL;
	}
	streams[0].stream = stdin;
	streams[0].ref_cnt = 1;
	streams[0].can_read = 1;
	streams[0].can_write = 0;
	streams[1].stream = stdout;
	streams[1].ref_cnt = 1;
	streams[1].can_read = 0;
	streams[1].can_write = 1;
	streams[2].stream = stderr;
	streams[2].ref_cnt = 1;
	streams[2].can_read = 0;
	streams[2].can_write = 1;
	fds[0] = &streams[0];
	fds[1] = &streams[1];
	fds[2] = &streams[2];
	return 1;
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
	uint8_t* data;
	size_t read_size;
	fd = readint(&ok1, regs[ESP] + 4, 4);
	buf = readint(&ok2, regs[ESP] + 8, 4);
	n = readint(&ok3, regs[ESP] + 12, 4);
	if (!(ok1 && ok2 && ok3)) {
		regs[EAX] = -1;
		return 1;
	}
	/* 入力元のチェック */
	if (FD_MAX <= fd || fds[fd] == NULL || !fds[fd]->can_read) {
		regs[EAX] = -1;
		return 1;
	}
	/* データを取得 */
	data = malloc(n);
	if (data == NULL) {
		perror("malloc");
		return -1;
	}
	read_size = fread(data, 1, n, fds[fd]->stream);
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

static int xv6_open(uint32_t regs[]) {
	uint32_t name_ptr, mode;
	int ok1, ok2;
	char* name;
	uint32_t i;
	stream_info* si;
	uint32_t fd;
	int want_read, want_write, want_create;
	name_ptr = readint(&ok1, regs[ESP] + 4, 4);
	mode = readint(&ok2, regs[ESP] + 8, 4);
	if (!(ok1 && ok2)) {
		regs[EAX] = -1;
		return 1;
	}

	/* ファイル名を取得する */
	name = NULL;
	i = 0;
	for (;;) {
		uint8_t c = 0;
		char* next_name;
		if (i == UINT32_MAX || !dmemory_is_allocated(name_ptr + i, 1)) {
			free(name);
			regs[EAX] = -1;
			return 1;
		}
		dmemory_read(&c, name_ptr + i, 1);
		next_name = realloc(name, i + 1);
		if (next_name == NULL) {
			perror("realloc");
			free(name);
			return -1;
		}
		name = next_name;
		name[i] = c;
		if (c == 0) break;
		if (i + name_ptr == UINT32_MAX) {
			free(name);
			regs[EAX] = -1;
			return 1;
		}
		i++;
	}

	/* ファイル情報の書き込み先を確保する */
	for (fd = 0; fd < FD_MAX; fd++) {
		if (fds[fd] == NULL) break;
	}
	for (si = NULL, i = 0; i < STREAM_MAX; i++) {
		if (streams[i].stream == NULL) {
			si = &streams[i];
			break;
		}
	}
	if (fd >= FD_MAX || si == NULL) {
		free(name);
		regs[EAX] = -1;
		return 1;
	}

	/* ファイルを開いて情報を登録する */
	want_read = ((mode & 3) != 1); /* 読み込みを有効化(O_WRONLYでない) */
	want_write = ((mode & 3) != 0); /* 書き込みを有効化(O_RDONLYでない) */
	want_create = ((mode & 0x200) != 0); /* 新規作成を有効化(O_CREATEを含む) */
	/* ファイルの内容を消さずに開くためにrを使う */
	si->stream = fopen(name, want_write ? "r+" : "r");
	if (si->stream == NULL && want_create) {
		/* 開けなかった場合、ファイルが無かったとみなしてwでの作成を試みる */
		si->stream = fopen(name, want_read ? "w+" : "w");
	}
	if (si->stream == NULL) {
		/* それでも開けなかったらエラー */
		free(name);
		regs[EAX] = -1;
		return 1;
	}
	/* ファイルが開けたので、その他の情報を登録する */
	si->ref_cnt = 1;
	si->can_read = want_read;
	si->can_write = want_write;
	si->prev_operation = POP_NONE;
	fds[fd] = si;

	/* 成功 */
	free(name);
	regs[EAX] = fd;
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
	/* 出力先のチェック */
	if (FD_MAX <= fd || fds[fd] == NULL || !fds[fd]->can_write) {
		regs[EAX] = -1;
		return 1;
	}
	/* データを取得して出力 */
	data = malloc(n);
	if (data == NULL) {
		perror("malloc");
		return -1;
	}
	dmemory_read(data, buf, n);
	if (fwrite(data, 1, n, fds[fd]->stream) != n) {
		regs[EAX] = -1;
		return 1;
	}
	free(data);
	/* 成功 */
	regs[EAX] = n;
	return 1;
}

static int xv6_close(uint32_t regs[]) {
	uint32_t fd;
	int ok;
	fd = readint(&ok, regs[ESP] + 4, 4);
	if (!ok) {
		regs[EAX] = -1;
		return 1;
	}
	if (FD_MAX <= fd || fds[fd] == NULL) {
		regs[EAX] = -1;
		return 1;
	}
	fds[fd]->ref_cnt--;
	if (fds[fd]->ref_cnt <= 0) {
		fclose(fds[fd]->stream);
		fds[fd]->stream = NULL;
	}
	fds[fd] = NULL;
	regs[EAX] = 0;
	return 1;
}

int xv6_syscall(uint32_t regs[]) {
	switch (regs[EAX]) {
		case 2: /* exit */
			return 0;
		case 5: /* read */
			return xv6_read(regs);
		case 12: /* sbrk */
			return xv6_sbrk(regs);
		case 15: /* open */
			return xv6_open(regs);
		case 16: /* write */
			return xv6_write(regs);
		case 21:
			return xv6_close(regs);
		default: /* 不正もしくは未実装 */
			regs[EAX] = -1;
			break;
	}
	return 1;
}
