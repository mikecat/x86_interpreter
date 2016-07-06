#include <stdio.h>
#include <inttypes.h>

uint8_t memory_access(uint32_t addr, uint8_t data, int we) {
	return 0;
}

#define CF 0x0001
#define PF 0x0004
#define AF 0x0010
#define ZF 0x0040
#define SF 0x0080
#define IF 0x0200
#define DF 0x0400
#define OF 0x0800

#define EAX 0
#define ECX 1
#define EDX 2
#define EBX 3
#define ESP 4
#define EBP 5
#define ESI 6
#define EDI 7
uint32_t regs[8];
uint32_t eip;
uint32_t eflags;

int step(void) {
	uint8_t fetch_data;

	int is_data_16bit = 0;
	int is_addr_16bit = 0;
	int is_rep = 0;
	int is_rep_while_zero = 0;

	enum {
		OP_ARITIMETIC,
		OP_XCHG,
		OP_PUSH,
		OP_POP,
		OP_PUSHA,
		OP_POPA,
		OP_STRING,
		OP_JUMP
	} op_kind = OP_ARITIMETIC; /* 命令の種類 */
	enum {
		OP_ADD,
		OP_ADC,
		OP_SUB,
		OP_SBB,
		OP_AND,
		OP_OR,
		OP_XOR,
		OP_CMP,
		OP_TEST,
		OP_READ_MODRM /* mod r/mの値を見て演算の種類を決める */
	} op_aritimetic_kind = OP_ADD; /* 演算命令の種類 */
	int op_width = 1; /* オペランドのバイト数 */
	int is_dest_reg = 0; /* 結果の書き込み先がr/mではなくregか */
	int jmp_take = 0; /* ジャンプを行うか */
	int use_mod_rm = 0; /* mod r/mを使うか */
	int use_imm = 0; /* 即値を使うか */

	/* オペランドの情報 */
	enum {
		OP_KIND_IMM,
		OP_KIND_MEM,
		OP_KIND_REG
	};
	uint32_t src_addr = 0;
	int src_kind = OP_KIND_IMM;
	int src_reg_index = 0;
	uint32_t dest_addr = 0;
	int dest_kind = OP_KIND_IMM;
	int dest_reg_index = 0;

	uint32_t imm_value = 0; /* 即値の値 */

	/* プリフィックスを解析する */
	for(;;) {
		/* 命令フェッチ */
		fetch_data = memory_access(eip, 0, 0);
		eip++;
		/* プリフィックスか判定 */
		if (fetch_data == 0x26 || /* ES override */
		fetch_data == 0x2E || /* CS override */
		fetch_data == 0x36 || /* SS override */
		fetch_data == 0x3E || /* DS override */
		fetch_data == 0x64 || /* FS override */
		fetch_data == 0x65 || /* GS override */
		fetch_data == 0x9B || /* wait */
		fetch_data == 0xF0 ) { /* lock */
			/* 無視 */
		} else if (fetch_data == 0x66) { /* operand-size override */
			is_data_16bit = 1;
		} else if (fetch_data == 0x67) { /* address-size override */
			is_addr_16bit = 1;
		} else if (fetch_data == 0xF2) { /* repnz */
			is_rep = 1;
			is_rep_while_zero = 0;
		} else if (fetch_data == 0xF3) { /* repz */
			is_rep = 1;
			is_rep_while_zero = 1;
		} else {
			break;
		}
	}

	/* オペコードを解析する */
	if (fetch_data == 0x0F) {
		
	} else {
		/* 前半のパターンに沿った演算命令 */
		if (fetch_data <= 0x3F && (fetch_data & 0x07) <= 0x05) {
			op_kind = OP_ARITIMETIC;
			/* オペランドを解析 */
			switch (fetch_data & 0x07) {
			case 0x00: /* r/m8, r8 */
				op_width = 1;
				is_dest_reg = 0;
				use_mod_rm = 1;
				break;
			case 0x01: /* r/m16/32, r16/32 */
				op_width = (is_data_16bit ? 2 : 4);
				is_dest_reg = 0;
				use_mod_rm = 1;
				break;
			case 0x02: /* r8, r/m8 */
				op_width = 1;
				is_dest_reg = 1;
				use_mod_rm = 1;
				break;
			case 0x03: /* r16/32, r/m16/32 */
				op_width = (is_data_16bit ? 2 : 4);
				is_dest_reg = 1;
				use_mod_rm = 1;
				break;
			case 0x04: /* AL, imm8 */
				op_width = 1;
				use_mod_rm = 0;
				use_imm = 1;
				src_kind = OP_KIND_IMM;
				dest_kind = OP_KIND_REG;
				dest_reg_index = EAX;
				break;
			case 0x05: /* eAX, imm16/32 */
				op_width = (is_data_16bit ? 2 : 4);
				use_mod_rm = 0;
				use_imm = 1;
				src_kind = OP_KIND_IMM;
				dest_kind = OP_KIND_REG;
				dest_reg_index = EAX;
				break;
			}
			/* 演算の種類を解析 */
			switch(fetch_data >> 3) {
			case 0: op_aritimetic_kind = OP_ADD; break;
			case 1: op_aritimetic_kind = OP_OR; break;
			case 2: op_aritimetic_kind = OP_ADC; break;
			case 3: op_aritimetic_kind = OP_SBB; break;
			case 4: op_aritimetic_kind = OP_AND; break;
			case 5: op_aritimetic_kind = OP_SUB; break;
			case 6: op_aritimetic_kind = OP_XOR; break;
			case 7: op_aritimetic_kind = OP_CMP; break;
			}
		}
	}

	/* mod r/mを解析する */

	/* SIBを解析する */

	/* dispを解析する */

	/* 即値を解析する */

	return 1;
}

int main(int argc, char *argv[]) {
	while(step());
	return 0;
}
