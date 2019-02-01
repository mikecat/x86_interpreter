#include <stdio.h>
#include <inttypes.h>
#include "dynamic_memory.h"
#include "read_raw.h"

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

void print_regs(FILE* fp) {
	fprintf(fp, "   EAX:%08"PRIx32" EBX:%08"PRIx32" ECX:%08"PRIx32" EDX:%08"PRIx32"\n",
		regs[EAX], regs[EBX], regs[ECX], regs[EDX]);
	fprintf(fp, "   ESI:%08"PRIx32" EDI:%08"PRIx32" ESP:%08"PRIx32" EBP:%08"PRIx32"\n",
		regs[ESI], regs[EDI], regs[ESP], regs[EBP]);
	fprintf(fp, "   EIP:%08"PRIx32"\n", eip);
	fprintf(fp, "EFLAGS:%08"PRIx32
		" (CF[%c] PF[%c] AF[%c] ZF[%c] SF[%c] IF[%c] DF[%c] OF[%c])\n", eflags,
		eflags & CF ? 'x' : ' ', eflags & PF ? 'x' : ' ', eflags & AF ? 'x' : ' ',
		eflags & ZF ? 'x' : ' ', eflags & SF ? 'x' : ' ', eflags & IF ? 'x' : ' ',
		eflags & DF ? 'x' : ' ', eflags & OF ? 'x' : ' ');
}

int memory_access(uint8_t* data_read, uint32_t addr, uint8_t data, int we) {
	if (dmemory_is_allocated(addr, 1)) {
		if (we) dmemory_write(&data, addr, 1);
		dmemory_read(data_read, addr, 1);
		return 1;
	} else {
		return 0;
	}
}

uint32_t step_memread(int* success, uint32_t inst_addr, uint32_t addr, int size) {
	uint32_t res = 0;
	int i;
	for (i = 0; i < size; i++) {
		uint32_t this_addr = addr + i;
		uint8_t value;
		if (!memory_access(&value, this_addr, 0, 0)) {
			fprintf(stderr, "failed to read memory %08"PRIx32" at %08"PRIx32"\n\n", this_addr, inst_addr);
			print_regs(stderr);
			*success = 0;
			return 0;
		}
		res |= value << (i * 8);
	}
	if (size < 4) {
		if (res & (UINT32_C(0x80) << ((size - 1) * 8))) {
			res |= UINT32_C(0xffffffff) << (size * 8);
		} else {
			res &= UINT32_C(0xffffffff) >> ((4 - size) * 8);
		}
	}
	*success = 1;
	return res;
}

int step_memwrite(uint32_t inst_addr, uint32_t addr, uint32_t value, int size) {
	int i;
	for (i = 0; i < size; i++) {
		uint32_t this_addr = addr + i;
		uint8_t value;
		if (!memory_access(&value, this_addr, (value >> (i * 8)) & 0xff, 1)) {
			fprintf(stderr, "failed to write memory %08"PRIx32" at %08"PRIx32"\n\n", this_addr, inst_addr);
			print_regs(stderr);
			return 0;
		}
	}
	return 1;
}

int step(void) {
	uint32_t inst_addr = eip; /* エラー時の検証用 */
	uint8_t fetch_data;
	int memread_ok;

	int is_data_16bit = 0;
	int is_addr_16bit = 0;
	int is_rep = 0;
	int is_rep_while_zero = 0;

	enum {
		OP_ARITIMETIC,
		OP_XCHG,
		OP_MOV,
		OP_LEA,
		OP_INCDEC,
		OP_PUSH,
		OP_POP,
		OP_PUSHA,
		OP_POPA,
		OP_PUSHF,
		OP_POPF,
		OP_STRING,
		OP_JUMP,
		OP_CBW,
		OP_CWD,
		OP_SAHF,
		OP_LAHF
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
	enum {
		OP_STR_MOV,
		OP_STR_CMP,
		OP_STR_STO,
		OP_STR_LOD,
		OP_STR_SCA
	} op_string_kind = OP_STR_MOV; /* ストリング命令の種類 */
	int op_width = 1; /* オペランドのバイト数 */
	int jmp_take = 0; /* ジャンプを行うか */
	int use_mod_rm = 0; /* mod r/mを使うか */
	int is_dest_reg = 0; /* mod r/mを使うとき、結果の書き込み先がr/mではなくregか */
	int use_imm = 0; /* 即値を使うか */
	int one_byte_imm = 0; /* 即値が1バイトか(偽 = オペランドのサイズ) */

	/* オペランドの情報 */
	enum {
		OP_KIND_IMM, /* 即値 (imm_value) */
		OP_KIND_MEM, /* メモリ上のデータ (*_addr) */
		OP_KIND_REG, /* AH/CH/DH/BHではないレジスタ上のデータ (*_reg_index) */
		OP_KIND_REG_HIGH8 /* レジスタAH/CH/DH/BH上のデータ (*_reg_index) */
	};
	uint32_t src_addr = 0;
	int src_kind = OP_KIND_IMM;
	int src_reg_index = 0;
	uint32_t dest_addr = 0;
	int dest_kind = OP_KIND_IMM;
	int dest_reg_index = 0;
	int need_dest_value = 0;

	uint32_t imm_value = 0; /* 即値の値 */

	/* プリフィックスを解析する */
	for(;;) {
		/* 命令フェッチ */
		fetch_data = step_memread(&memread_ok, inst_addr, eip, 1);
		if (!memread_ok) return 0;
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
		fprintf(stderr, "unsupported opcode %02"PRIx8" at %08"PRIx32"\n\n", fetch_data, inst_addr);
		print_regs(stderr);
		return 0;
	} else {
		if (fetch_data <= 0x3F && (fetch_data & 0x07) <= 0x05) {
			/* パターンに沿った演算命令 */
			op_kind = OP_ARITIMETIC;
			/* オペランドを解析 */
			switch (fetch_data & 0x07) {
			case 0x00: /* r/m8, r8 */
				op_width = 1;
				use_mod_rm = 1;
				is_dest_reg = 0;
				break;
			case 0x01: /* r/m16/32, r16/32 */
				op_width = (is_data_16bit ? 2 : 4);
				use_mod_rm = 1;
				is_dest_reg = 0;
				break;
			case 0x02: /* r8, r/m8 */
				op_width = 1;
				use_mod_rm = 1;
				is_dest_reg = 1;
				break;
			case 0x03: /* r16/32, r/m16/32 */
				op_width = (is_data_16bit ? 2 : 4);
				use_mod_rm = 1;
				is_dest_reg = 1;
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
			need_dest_value = 1;
		} else if (0x40 <= fetch_data && fetch_data < 0x50) {
			/* INC/DEC */
			op_kind = OP_INCDEC;
			op_width = (is_data_16bit ? 2 : 4);
			use_mod_rm = 0;
			src_kind = OP_KIND_IMM;
			dest_kind = OP_KIND_REG;
			dest_reg_index = fetch_data & 0x07;
			imm_value = (fetch_data < 0x48 ? 1 : -1);
			need_dest_value = 1;
		} else if (0x50 <= fetch_data && fetch_data < 0x60) {
			/* PUSH/POP */
			op_width = (is_data_16bit ? 2 : 4);
			use_mod_rm = 0;
			if (fetch_data < 0x58) {
				op_kind = OP_PUSH;
				src_kind = OP_KIND_REG;
				src_reg_index = fetch_data & 0x07;
			} else {
				op_kind = OP_POP;
				dest_kind = OP_KIND_REG;
				dest_reg_index = fetch_data & 0x07;
			}
		} else if (fetch_data == 0x60) {
			/* PUSHA */
			op_kind = OP_PUSHA;
		} else if (fetch_data == 0x61) {
			/* POPA */
			op_kind = OP_POPA;
		} else if (fetch_data == 0x68) {
			/* PUSH imm16/32 */
			op_kind = OP_PUSH;
			op_width = (is_data_16bit ? 2 : 4);
			use_imm = 1;
			src_kind = OP_KIND_IMM;
		} else if (fetch_data == 0x6A) {
			/* PUSH imm8 */
			op_kind = OP_PUSH;
			op_width = 1;
			use_imm = 1;
			src_kind = OP_KIND_IMM;
		} else if (0x70 <= fetch_data && fetch_data < 0x80) {
			/* 条件分岐 */
			op_kind = OP_JUMP;
			switch (fetch_data & 0x0E) {
			case 0x0: jmp_take = (eflags & OF); break; /* JO */
			case 0x2: jmp_take = (eflags & CF); break; /* JB */
			case 0x4: jmp_take = (eflags & ZF); break; /* JZ */
			case 0x6: jmp_take = (eflags & CF) || (eflags & ZF); break; /* JBE */
			case 0x8: jmp_take = (eflags & SF); break; /* JS */
			case 0xA: jmp_take = (eflags & PF); break; /* JP */
			case 0xC: jmp_take = ((eflags & SF) != 0) != ((eflags & OF) != 0); break; /* JL */
			case 0xE: jmp_take = (eflags & ZF) || (((eflags & SF) != 0) != ((eflags & OF) != 0)); break; /* JLE */
			}
			if (fetch_data & 0x01) jmp_take = !jmp_take;
			op_width = 1;
			use_imm = 1;
		} else if (0x80 <= fetch_data && fetch_data <= 0x83) {
			/* 定数との演算 */
			op_kind = OP_ARITIMETIC;
			op_aritimetic_kind = OP_READ_MODRM;
			op_width = (fetch_data & 1 ? (is_data_16bit ? 2 : 4) : 1);
			use_mod_rm = 1;
			is_dest_reg = 0;
			use_imm = 1;
			src_kind = OP_KIND_IMM;
			if (fetch_data == 0x83) one_byte_imm = 1;
			need_dest_value = 1;
		} else if (0x84 <= fetch_data && fetch_data <= 0x8B) {
			/* 演算 */
			if ((fetch_data & 0xFE) == 0x84) {
				op_kind = OP_ARITIMETIC;
				op_aritimetic_kind = OP_TEST;
				need_dest_value = 1;
			} else if ((fetch_data & 0xFE) == 0x86) {
				op_kind = OP_XCHG;
				need_dest_value = 1;
			} else {
				op_kind = OP_MOV;
			}
			op_width = (fetch_data & 1 ? (is_data_16bit ? 2 : 4) : 1);
			use_mod_rm = 1;
			is_dest_reg = (fetch_data & 2);
		} else if (fetch_data == 0x8D) {
			/* LEA */
			op_kind = OP_LEA;
			op_width = 4;
			use_mod_rm = 1;
			is_dest_reg = 1;
		} else if (fetch_data == 0x8F) {
			/* POP r/m16/32 */
			op_kind = OP_POP;
			op_width = (is_data_16bit ? 2 : 4);
			use_mod_rm = 1;
			is_dest_reg = 0;
		} else if (0x90 <= fetch_data && fetch_data < 0x98) {
			/* XCHG r16/32, eAX */
			op_kind = OP_XCHG;
			op_width = (is_data_16bit ? 2 : 4);
			src_kind = OP_KIND_REG;
			src_reg_index = (fetch_data & 0x07);
			dest_kind = OP_KIND_REG;
			dest_reg_index = EAX;
			need_dest_value = 1;
		} else if (fetch_data == 0x98) {
			/* CBW/CWDE */
			op_kind = OP_CBW;
			op_width = (is_data_16bit ? 2 : 4);
			src_kind = OP_KIND_REG;
			src_reg_index = EAX;
			dest_kind = OP_KIND_REG;
			dest_reg_index = EAX;
		} else if (fetch_data == 0x99) {
			/* CWD/CDQ */
			op_kind = OP_CWD;
			op_width = (is_data_16bit ? 2 : 4);
			src_kind = OP_KIND_REG;
			src_reg_index = EAX;
			dest_kind = OP_KIND_REG;
			dest_reg_index = EDX;
		} else if (fetch_data == 0x9C) {
			/* PUSHF */
			op_kind = OP_PUSHF;
		} else if (fetch_data == 0x9D) {
			/* POPF */
			op_kind = OP_POPF;
		} else if (fetch_data == 0x9E) {
			/* SAHF */
			op_kind = OP_SAHF;
			op_width = 1;
			src_kind = OP_KIND_REG;
			src_reg_index = EAX;
		} else if (fetch_data == 0x9F) {
			/* LAHF */
			op_kind = OP_LAHF;
			op_width = 1;
			dest_kind = OP_KIND_REG;
			dest_reg_index = EAX;
		} else if (0xA4 <= fetch_data && fetch_data <= 0xAF && (fetch_data & 0xFE) != 0xA8) {
			/* ストリング命令 */
			op_kind = OP_STRING;
			switch (fetch_data & 0xFE) {
			case 0xA4: op_string_kind = OP_STR_MOV; break;
			case 0xA6: op_string_kind = OP_STR_CMP; break;
			case 0xAA: op_string_kind = OP_STR_STO; break;
			case 0xAC: op_string_kind = OP_STR_LOD; break;
			case 0xAE: op_string_kind = OP_STR_SCA; break;
			}
			op_width = (fetch_data & 1 ? (is_data_16bit ? 2 : 4) : 1);
		} else if (fetch_data == 0xA8 || fetch_data == 0xA9) {
			/* TEST AL/AX/EAX, imm8/imm16/imm32 */
			op_kind = OP_ARITIMETIC;
			op_aritimetic_kind = OP_TEST;
			op_width = (fetch_data & 1 ? (is_data_16bit ? 2 : 4) : 1);
			use_imm = 1;
			src_kind = OP_KIND_IMM;
			dest_kind = OP_KIND_REG;
			dest_reg_index = EAX;
			need_dest_value = 1;
		} else {
			fprintf(stderr, "unsupported opcode %02"PRIx8" at %08"PRIx32"\n\n", fetch_data, inst_addr);
			print_regs(stderr);
			return 0;
		}
	}

	/* mod r/mを解析する */
	int use_sib = 0; /* sibを使うか */
	int disp_size = 0; /* dispのバイト数 */

	int reg_index = 0; /* mod r/m中のregから取得したレジスタ番号 */
	int reg_is_high = 0; /* mod r/m中のregがAH/CH/DH/BHか */
	int modrm_reg_index = 0; /* mod r/m中のmod r/mで使うレジスタ番号 */
	int modrm_reg_is_high = 0; /* mod r/m中のmod r/mで使うレジスタがAH/CH/DH/BHか */
	int modrm_reg2_index = 0; /* mod r/m中のmod r/mで使う2個目のレジスタ番号 */
	int modrm_reg2_scale = 0; /* mod r/m中のmod r/mで使う2個目のレジスタの係数 */
	int modrm_no_reg = 0; /* mod r/m中のmod r/mの実効アドレスで最初のレジスタを使わないか */
	int modrm_is_mem = 0; /* mod r/m中のmod r/mがメモリか */

	if (use_mod_rm) {
		uint8_t mod_rm = step_memread(&memread_ok, inst_addr, eip, 1);
		if (!memread_ok) return 0;
		eip++;

		int mod = (mod_rm >> 6) & 3;
		int reg = (mod_rm >> 3) & 7;
		int rm  =  mod_rm       & 7;

		if (op_aritimetic_kind == OP_READ_MODRM) {
			/* 「mod r/mを見て決定する」演算を決定する */
			static const int kind_table[] = {
				OP_ADD, OP_OR, OP_ADC, OP_SBB, OP_AND, OP_SUB, OP_XOR, OP_CMP
			};
			op_aritimetic_kind = kind_table[reg];
		}

		if (op_width == 1) {
			if (reg < 4) {
				reg_index = reg;
				reg_is_high = 0;
			} else {
				reg_index = reg & 3;
				reg_is_high = 1;
			}
		} else {
			reg_index = reg;
			reg_is_high = 0;
		}

		if (mod == 3) {
			/* レジスタオペランド */
			if (op_width == 1) {
				if (rm < 4) {
					modrm_reg_index = rm;
					modrm_reg_is_high = 0;
				} else {
					modrm_reg_index = rm & 3;
					modrm_reg_is_high = 1;
				}
			} else {
				modrm_reg_index = rm;
				modrm_reg_is_high = 0;
			}
		} else {
			/* メモリオペランド */
			if (is_addr_16bit) {
				/* 16-bit mod r/m */
				modrm_is_mem = 1;
				if (mod == 0 && rm == 5) {
					modrm_no_reg = 1;
					disp_size = 2;
				} else {
					switch (rm) {
						case 0: case 1: case 7: modrm_reg_index = EBX; break;
						case 2: case 3: case 6: modrm_reg_index = EBP; break;
						case 4: modrm_reg_index = ESI; break;
						case 5: modrm_reg_index = EDI; break;
					}
					if (rm == 0 || rm == 2) {
						modrm_reg2_index = ESI;
						modrm_reg2_scale = 1;
					} else if (rm == 1 || rm == 3) {
						modrm_reg2_index = EDI;
						modrm_reg2_scale = 1;
					}
					switch (mod) {
						case 0: disp_size = 0; break;
						case 1: disp_size = 1; break;
						case 2: disp_size = 2; break;
					}
				}
			} else {
				/* 32-bit mod r/m */
				if (mod == 0 && rm == 5) {
					modrm_no_reg = 1;
					disp_size = 4;
				} else {
					if (rm == 4) {
						use_sib = 1;
						if (mod == 0) modrm_no_reg = 1;
					} else {
						modrm_reg_index = rm;
					}
					switch (mod) {
						case 0: disp_size = 0; break;
						case 1: disp_size = 1; break;
						case 2: disp_size = 4; break;
					}
				}
			}
		}
	}

	/* SIBを解析する */
	if (use_sib) {
		uint8_t sib = step_memread(&memread_ok, inst_addr, eip, 1);
		if (!memread_ok) return 0;
		eip++;

		int ss  = (sib >> 6) & 3;
		int idx = (sib >> 3) & 7;
		int r32 =  sib       & 7;

		if (idx == 4) {
			fprintf(stderr, "sib index=4 (none) detected at %08"PRIx32"\n\n", inst_addr);
			print_regs(stderr);
			return 0;
		}
		modrm_reg_index = r32;
		modrm_reg2_index = idx;
		switch (ss) {
			case 0: modrm_reg2_scale = 1; break;
			case 1: modrm_reg2_scale = 2; break;
			case 2: modrm_reg2_scale = 4; break;
			case 3: modrm_reg2_scale = 8; break;
		}
	}

	/* dispを解析する */
	uint32_t disp = 0;
	if (disp_size > 0) {
		disp = step_memread(&memread_ok, inst_addr, eip, disp_size);
		if (!memread_ok) return 0;
		eip += disp_size;
	}

	/* mod r/m、sib、dispに基づき、オペランドを決定する */
	if (use_mod_rm) {
		/* オペランドの参照先決定 */
		int reg_kind = reg_is_high ? OP_KIND_REG_HIGH8 : OP_KIND_REG;
		int modrm_kind;
		uint32_t modrm_addr = 0;
		if (modrm_is_mem) {
			uint32_t mask = is_addr_16bit ? UINT32_C(0xffff) : UINT32_C(0xffffffff);
			uint32_t reg1_value = regs[modrm_reg_index] & mask;
			uint32_t reg2_value = regs[modrm_reg2_index] & mask;
			modrm_kind = OP_KIND_MEM;
			modrm_addr = ((modrm_no_reg ? 0 : reg1_value) + (reg2_value * modrm_reg2_scale) + disp) & mask;
		} else {
			modrm_kind = modrm_reg_is_high ? OP_KIND_REG_HIGH8 : OP_KIND_REG;
		}

		/* オペランドの割り当ての決定 */
		/* srcの決定 */
		if (!use_imm) {
			if (is_dest_reg) {
				/* destがregなので、srcはmod r/m */
				src_kind = modrm_kind;
				src_reg_index = modrm_reg_index;
				src_addr = modrm_addr;
			} else {
				/* srcがreg */
				src_kind = reg_kind;
				src_reg_index = reg_index;
			}
		}
		/* destの決定 */
		if (is_dest_reg) {
			dest_kind = reg_kind;
			dest_reg_index = reg_index;
		} else {
			dest_kind = modrm_kind;
			dest_reg_index = modrm_reg_index;
			dest_addr = modrm_addr;
		}
	}

	/* 即値を解析する */
	if (use_imm) {
		int imm_size = one_byte_imm ? 1 : op_width;
		imm_value = step_memread(&memread_ok, inst_addr, eip, imm_size);
		if (!memread_ok) return 0;
		eip += disp_size;
	}

	/* オペランドを読み込む */
	uint32_t src_value = 0;
	uint32_t dest_value = 0;

	switch (src_kind) {
	case OP_KIND_IMM:
		src_value = imm_value;
		break;
	case OP_KIND_MEM:
		src_value = step_memread(&memread_ok, inst_addr, src_addr, op_width);
		if (!memread_ok) return 0;
		break;
	case OP_KIND_REG:
		src_value = regs[src_reg_index];
		break;
	case OP_KIND_REG_HIGH8:
		src_value = regs[src_reg_index] >> 8;
		break;
	}
	if (op_width < 4) {
		if (src_value & (UINT32_C(0x80) << ((op_width - 1) * 8))) {
			src_value |= UINT32_C(0xffffffff) << (op_width * 8);
		} else {
			src_value &= UINT32_C(0xffffffff) >> ((4 - op_width) * 8);
		}
	}

	if (need_dest_value) {
		switch (dest_kind) {
		case OP_KIND_IMM:
			dest_value = imm_value;
			break;
		case OP_KIND_MEM:
			dest_value = step_memread(&memread_ok, inst_addr, dest_addr, op_width);
			if (!memread_ok) return 0;
			break;
		case OP_KIND_REG:
			dest_value = regs[dest_reg_index];
			break;
		case OP_KIND_REG_HIGH8:
			dest_value = regs[dest_reg_index] >> 8;
			break;
		}
		if (op_width < 4) {
			if (dest_value & (UINT32_C(0x80) << ((op_width - 1) * 8))) {
				dest_value |= UINT32_C(0xffffffff) << (op_width * 8);
			} else {
				dest_value &= UINT32_C(0xffffffff) >> ((4 - op_width) * 8);
			}
		}
	}

	/* 計算をする */

	/* 計算結果を書き込む */

	return 1;
}

int main(int argc, char *argv[]) {
	if (argc >= 2) read_raw(argv[1]);
	while(step());
	return 0;
}
