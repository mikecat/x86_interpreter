#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "x86_regs.h"
#include "dynamic_memory.h"
#include "read_raw.h"
#include "read_elf.h"
#include "read_pe.h"
#include "xv6_syscall.h"
#include "pe_import.h"

static int use_xv6_syscall = 0;
static int use_pe_import = 0;
static pe_import_params import_params;

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

static uint32_t step_memread(int* success, uint32_t inst_addr, uint32_t addr, int size) {
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

static int step_memwrite(uint32_t inst_addr, uint32_t addr, uint32_t value, int size) {
	int i;
	for (i = 0; i < size; i++) {
		uint32_t this_addr = addr + i;
		uint8_t dummy_read;
		if (!memory_access(&dummy_read, this_addr, (value >> (i * 8)) & 0xff, 1)) {
			fprintf(stderr, "failed to write memory %08"PRIx32" at %08"PRIx32"\n\n", this_addr, inst_addr);
			print_regs(stderr);
			return 0;
		}
	}
	return 1;
}

static int step_push(uint32_t inst_addr, uint32_t value, int op_width, int is_addr_16bit) {
	uint32_t addr = regs[ESP];
	if (is_addr_16bit) addr &= 0xffff;
	addr -= op_width;
	if (!step_memwrite(inst_addr, addr, value, op_width)) return 0;
	if (is_addr_16bit) {
		regs[ESP] = (regs[ESP] & UINT32_C(0xffff0000)) | (addr & 0xffff);
	} else {
		regs[ESP] = addr;
	}
	return 1;
}

static uint32_t step_pop(int* success, uint32_t inst_addr, int op_width, int is_addr_16bit) {
	int memread_ok = 0;
	uint32_t next_esp;
	uint32_t value = step_memread(&memread_ok, inst_addr,
		is_addr_16bit ? regs[ESP] & 0xffff : regs[ESP], op_width);
	if (!memread_ok) {
		*success = 0;
		return 0;
	}
	next_esp = regs[ESP] + op_width;
	if (is_addr_16bit) {
		regs[ESP] = (regs[ESP] & UINT32_C(0xffff0000)) | (next_esp & 0xffff);
	} else {
		regs[ESP] = next_esp;
	}
	*success = 1;
	return value;
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
		OP_ARITHMETIC,
		OP_SHIFT,
		OP_XCHG,
		OP_MOV,
		OP_MOVZX,
		OP_MOVSX,
		OP_SETCC,
		OP_LEA,
		OP_INCDEC,
		OP_NOT,
		OP_MUL,
		OP_IMUL,
		OP_DIV,
		OP_IDIV,
		OP_PUSH,
		OP_POP,
		OP_PUSHA,
		OP_POPA,
		OP_PUSHF,
		OP_POPF,
		OP_STRING,
		OP_CALL,
		OP_JUMP,
		OP_CALL_ABSOLUTE,
		OP_JUMP_ABSOLUTE,
		OP_CALL_FAR,
		OP_JUMP_FAR,
		OP_CBW,
		OP_CWD,
		OP_SAHF,
		OP_LAHF,
		OP_RETN,
		OP_LEAVE,
		OP_INT,
		OP_INTO,
		OP_IRET,
		OP_LOOP,
		OP_IN,
		OP_OUT,
		OP_HLT,
		OP_CMC,
		OP_SET_FLAG,
		OP_CLEAR_FLAG,
		OP_FPU,
	} op_kind = OP_ARITHMETIC; /* 命令の種類 */
	enum {
		OP_ADD, OP_ADC, OP_SUB, OP_SBB, OP_AND, OP_OR, OP_XOR, OP_CMP, OP_TEST, OP_NEG,
		OP_READ_MODRM, /* mod r/mの値を見て演算の種類を決める */
		OP_READ_MODRM_MUL, /* mod r/mの値を見て演算の種類を決める(MUL系) */
		OP_READ_MODRM_INC /* mod r/mの値を見て演算の種類を決める(INC系) */
	} op_arithmetic_kind = OP_ADD; /* 演算命令の種類 */
	enum {
		OP_ROL, OP_ROR, OP_RCL, OP_RCR, OP_SHL, OP_SHR, OP_SAR,
		OP_SHLD, OP_SHRD,
		OP_READ_MODRM_SHIFT /* mod r/mの値を見て演算の種類を決める(シフト系) */
	} op_shift_kind = OP_ROL;
	enum {
		OP_STR_MOV,
		OP_STR_CMP,
		OP_STR_STO,
		OP_STR_LOD,
		OP_STR_SCA,
		OP_STR_IN,
		OP_STR_OUT
	} op_string_kind = OP_STR_MOV; /* ストリング命令の種類 */
	int op_width = 1; /* オペランドのバイト数 */
	int jmp_take = 0; /* ジャンプを行うか */
	int use_mod_rm = 0; /* mod r/mを使うか */
	int is_dest_reg = 0; /* mod r/mを使うとき、結果の書き込み先がr/mではなくregか */
	int modrm_disable_src = 0; /* mod r/mを使う時、srcをmod r/mから設定するのをやめるか */
	int direct_disp_size = 0; /* moffsを使うとき、そのサイズ */
	int is_dest_direct_disp = 0; /* moffsをdestに使うか(sizeが0でないとき、偽 = srcに使う) */
	int use_imm = 0; /* 即値を使うか */
	int one_byte_imm = 0; /* 即値が1バイトか(偽 = オペランドのサイズ) */
	int op_fpu_kind = 0; /* FPU系命令のカテゴリ */
	int imul_store_upper = 0; /* IMUL命令において、上位の値を保存するか */
	int imul_enable_dest = 0; /* IMUL命令において、destの指定を有効にするか(偽 = AL/AX/EAX固定) */

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

	if (use_pe_import && import_params.iat_size >= 4 &&
	import_params.iat_addr <= eip && eip - import_params.iat_addr < import_params.iat_size) {
		int ret = pe_import(&eip, regs);
		if (ret == 0) return 0;
		if (ret < 0) {
			print_regs(stderr);
			return 0;
		}
		return 1;
	}

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

#define SET_JMP_TAKE \
	switch (fetch_data & 0x0E) { \
	case 0x0: jmp_take = (eflags & OF); break; /* JO */ \
	case 0x2: jmp_take = (eflags & CF); break; /* JB */ \
	case 0x4: jmp_take = (eflags & ZF); break; /* JZ */ \
	case 0x6: jmp_take = (eflags & CF) || (eflags & ZF); break; /* JBE */ \
	case 0x8: jmp_take = (eflags & SF); break; /* JS */ \
	case 0xA: jmp_take = (eflags & PF); break; /* JP */ \
	case 0xC: jmp_take = ((eflags & SF) != 0) != ((eflags & OF) != 0); break; /* JL */ \
	case 0xE: jmp_take = (eflags & ZF) || (((eflags & SF) != 0) != ((eflags & OF) != 0)); break; /* JLE */ \
	} \
	if (fetch_data & 0x01) jmp_take = !jmp_take;

	/* オペコードを解析する */
	if (fetch_data == 0x0F) {
		fetch_data = step_memread(&memread_ok, inst_addr, eip, 1);
		if (!memread_ok) return 0;
		eip++;
		if ((fetch_data & 0xF0) == 0x80) {
			/* Jcc rel16/32 */
			op_kind = OP_JUMP;
			op_width = is_data_16bit ? 2 : 4;
			use_imm = 1;
			SET_JMP_TAKE
		} else if ((fetch_data & 0xF0) == 0x90) {
			/* SETcc */
			op_kind = OP_SETCC;
			op_width = 1;
			use_mod_rm = 1;
			is_dest_reg = 0;
			SET_JMP_TAKE
		} else if ((fetch_data & 0xFE) == 0xA4 || (fetch_data & 0xFE) == 0xAC) {
			/* SHLD/SHRD */
			op_kind = OP_SHIFT;
			op_shift_kind = (fetch_data & 8) ? OP_SHRD : OP_SHLD;
			op_width = (is_data_16bit ? 2 : 4);
			use_mod_rm = 1;
			is_dest_reg = 0;
			if ((fetch_data & 1) == 0) {
				use_imm = 1;
				one_byte_imm = 1;
			}
			need_dest_value = 1;
		} else if (fetch_data == 0xAF) {
			/* IMUL r16/32, r/m16/32 */
			op_kind = OP_IMUL;
			op_width = (is_data_16bit ? 2 : 4);
			use_mod_rm = 1;
			is_dest_reg = 1;
			imul_enable_dest = 1;
		} else if ((fetch_data & 0xFE) == 0xB6) {
			/* MOVZX */
			op_kind = OP_MOVZX;
			op_width = (fetch_data & 0x01) ? 2 : 1;
			use_mod_rm = 1;
			is_dest_reg = 1;
		} else if ((fetch_data & 0xFE) == 0xBE) {
			/* MOVSX */
			op_kind = OP_MOVSX;
			op_width = (fetch_data & 0x01) ? 2 : 1;
			use_mod_rm = 1;
			is_dest_reg = 1;
		} else {
			fprintf(stderr, "unsupported opcode \"0f %02"PRIx8"\" at %08"PRIx32"\n\n", fetch_data, inst_addr);
			print_regs(stderr);
			return 0;
		}
	} else {
		if (fetch_data <= 0x3F && (fetch_data & 0x07) <= 0x05) {
			/* パターンに沿った演算命令 */
			op_kind = OP_ARITHMETIC;
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
			case 0: op_arithmetic_kind = OP_ADD; break;
			case 1: op_arithmetic_kind = OP_OR; break;
			case 2: op_arithmetic_kind = OP_ADC; break;
			case 3: op_arithmetic_kind = OP_SBB; break;
			case 4: op_arithmetic_kind = OP_AND; break;
			case 5: op_arithmetic_kind = OP_SUB; break;
			case 6: op_arithmetic_kind = OP_XOR; break;
			case 7: op_arithmetic_kind = OP_CMP; break;
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
		} else if (fetch_data == 0x69) {
			/* IMUL r16/32, r/m16/32, imm16/32 */
			op_kind = OP_IMUL;
			op_width = (is_data_16bit ? 2 : 4);
			use_mod_rm = 1;
			is_dest_reg = 1;
			use_imm = 1;
			imul_enable_dest = 1;
		} else if (fetch_data == 0x6A) {
			/* PUSH imm8 */
			op_kind = OP_PUSH;
			op_width = (is_data_16bit ? 2 : 4);
			use_imm = 1;
			one_byte_imm = 1;
			src_kind = OP_KIND_IMM;
		} else if (fetch_data == 0x6B) {
			/* IMUL r16/32, r/m16/32, imm8 */
			op_kind = OP_IMUL;
			op_width = (is_data_16bit ? 2 : 4);
			use_mod_rm = 1;
			is_dest_reg = 1;
			use_imm = 1;
			one_byte_imm = 1;
			imul_enable_dest = 1;
		} else if ((fetch_data & 0xFC) == 0x6C) {
			/* INS/OUTS */
			op_kind = OP_STRING;
			op_string_kind = (fetch_data & 0x03) < 2 ? OP_STR_IN : OP_STR_OUT;
			op_width = (fetch_data & 0x01) ? (is_data_16bit ? 2 : 4) : 1;
		} else if ((0x70 <= fetch_data && fetch_data < 0x80) || fetch_data == 0xE3 || fetch_data == 0xEB) {
			/* 分岐 */
			op_kind = OP_JUMP;
			if (fetch_data == 0xE3) {
				/* JCXZ */
				jmp_take = ((is_data_16bit ? regs[ECX] & 0xffff : regs[ECX]) == 0);
			} else if (fetch_data == 0xEB) {
				/* JMP */
				jmp_take = 1;
			} else {
				SET_JMP_TAKE
			}
			op_width = 1;
			use_imm = 1;
		} else if (0x80 <= fetch_data && fetch_data <= 0x83) {
			/* 定数との演算 */
			op_kind = OP_ARITHMETIC;
			op_arithmetic_kind = OP_READ_MODRM;
			op_width = (fetch_data & 1 ? (is_data_16bit ? 2 : 4) : 1);
			use_mod_rm = 1;
			modrm_disable_src = 1;
			is_dest_reg = 0;
			use_imm = 1;
			src_kind = OP_KIND_IMM;
			if (fetch_data == 0x83) one_byte_imm = 1;
			need_dest_value = 1;
		} else if (0x84 <= fetch_data && fetch_data <= 0x8B) {
			/* 演算 */
			if ((fetch_data & 0xFE) == 0x84) {
				op_kind = OP_ARITHMETIC;
				op_arithmetic_kind = OP_TEST;
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
			modrm_disable_src = 1;
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
			op_width = (is_data_16bit ? 2 : 4);
		} else if (fetch_data == 0x9D) {
			/* POPF */
			op_kind = OP_POPF;
			op_width = (is_data_16bit ? 2 : 4);
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
		} else if ((fetch_data & 0xFC) == 0xA0) {
			/* MOV with moffs */
			op_kind = OP_MOV;
			op_width = (fetch_data & 0x01) ? (is_data_16bit ? 2 : 4) : 1;
			direct_disp_size = (is_addr_16bit ? 2 : 4);
			if ((fetch_data & 0x2) == 0) {
				is_dest_direct_disp = 0;
				dest_kind = OP_KIND_REG;
				dest_reg_index = EAX;
			} else {
				is_dest_direct_disp = 1;
				src_kind = OP_KIND_REG;
				src_reg_index = EAX;
			}
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
			if (op_string_kind == OP_STR_STO || op_string_kind == OP_STR_SCA) {
				src_kind = OP_KIND_REG;
				src_reg_index = EAX;
			} else if (op_string_kind == OP_STR_LOD) {
				dest_kind = OP_KIND_REG;
				dest_reg_index = EAX;
			}
		} else if (fetch_data == 0xA8 || fetch_data == 0xA9) {
			/* TEST AL/AX/EAX, imm8/imm16/imm32 */
			op_kind = OP_ARITHMETIC;
			op_arithmetic_kind = OP_TEST;
			op_width = (fetch_data & 1 ? (is_data_16bit ? 2 : 4) : 1);
			use_imm = 1;
			src_kind = OP_KIND_IMM;
			dest_kind = OP_KIND_REG;
			dest_reg_index = EAX;
			need_dest_value = 1;
		} else if (0xB0 <= fetch_data && fetch_data <= 0xBF) {
			/* MOV r, imm */
			op_kind = OP_MOV;
			op_width = (fetch_data & 0x08 ? (is_data_16bit ? 2 : 4) : 1);
			use_imm = 1;
			src_kind = OP_KIND_IMM;
			dest_kind = OP_KIND_REG;
			dest_reg_index = fetch_data & 0x07;
		} else if (fetch_data == 0xC0 || fetch_data == 0xC1 || (0xD0 <= fetch_data && fetch_data <= 0xD3)) {
			/* シフト */
			op_kind = OP_SHIFT;
			op_shift_kind = OP_READ_MODRM_SHIFT;
			op_width = (fetch_data & 1 ? (is_data_16bit ? 2 : 4) : 1);
			use_mod_rm = 1;
			modrm_disable_src = 1;
			is_dest_reg = 0;
			switch (fetch_data & 0xFE) {
			case 0xC0:
				use_imm = 1;
				src_kind = OP_KIND_IMM;
				one_byte_imm = 1;
				break;
			case 0xD0:
				src_kind = OP_KIND_IMM;
				imm_value = 1;
				break;
			case 0xD2:
				src_kind = OP_KIND_REG;
				src_reg_index = ECX;
				break;
			}
			need_dest_value = 1;
		} else if (fetch_data == 0xC2 || fetch_data == 0xC3) {
			/* RETN */
			op_kind = OP_RETN;
			op_width = 2;
			if (fetch_data == 0xC2) {
				use_imm = 1;
			} else {
				imm_value = 0;
			}
		} else if (fetch_data == 0xC6 || fetch_data == 0xC7) {
			/* MOV r, imm */
			op_kind = OP_MOV;
			op_width = (fetch_data & 1 ? (is_data_16bit ? 2 : 4) : 1);
			use_mod_rm = 1;
			modrm_disable_src = 1;
			is_dest_reg = 0;
			use_imm = 1;
			src_kind = OP_KIND_IMM;
			need_dest_value = 1;
		} else if (fetch_data == 0xC9) {
			/* LEAVE */
			op_kind = OP_LEAVE;
			op_width = is_data_16bit ? 2 : 4;
		} else if (fetch_data == 0xCC || fetch_data == 0xCD) {
			/* INT */
			op_kind = OP_INT;
			op_width = 1;
			if (fetch_data == 0xCC) {
				imm_value = 3;
			} else {
				use_imm = 1;
			}
		} else if (fetch_data == 0xCE) {
			/* INTO */
			op_kind = OP_INTO;
		} else if (fetch_data == 0xCF) {
			/* IRET */
			op_kind = OP_IRET;
		} else if ((fetch_data & 0xF8) == 0xD8) {
			/* FPU instructions */
			op_kind = OP_FPU;
			use_mod_rm = 1;
			op_fpu_kind = fetch_data & 0x07;
		} else if (fetch_data == 0xE0 || fetch_data == 0xE1 || fetch_data == 0xE2) {
			/* LOOP* */
			op_kind = OP_LOOP;
			use_imm = 1;
			one_byte_imm = 1;
			op_width = is_data_16bit ? 2 : 4;
			src_kind = OP_KIND_REG;
			src_reg_index = ECX;
			dest_kind = OP_KIND_REG;
			dest_reg_index = ECX;
			switch (fetch_data) {
			case 0xE0: jmp_take = !(eflags & ZF); break;
			case 0xE1: jmp_take = (eflags & ZF); break;
			case 0xE2: jmp_take = 1; break;
			}
		} else if ((fetch_data & 0xFC) == 0xE4 || (fetch_data & 0xFC) == 0xEC) {
			/* IN/OUT */
			op_width = (fetch_data & 0x01) ? (is_data_16bit ? 2 : 4) : 1;
			if (fetch_data <= 0xE7) {
				use_imm = 1;
				one_byte_imm = 1;
			}
			if ((fetch_data & 0x03) < 2) {
				op_kind = OP_IN;
				src_kind = OP_KIND_REG;
				src_reg_index = EAX;
				if (fetch_data <= 0xE7) {
					dest_kind = OP_KIND_IMM;
				} else {
					dest_kind = OP_KIND_REG;
					dest_reg_index = EDX;
					need_dest_value = 1;
				}
			} else {
				op_kind = OP_OUT;
				dest_kind = OP_KIND_REG;
				dest_reg_index = EAX;
				if (fetch_data <= 0xE7) {
					src_kind = OP_KIND_IMM;
				} else {
					src_kind = OP_KIND_REG;
					src_reg_index = EDX;
				}
			}
		} else if (fetch_data == 0xE8) {
			/* CALL */
			op_kind = OP_CALL;
			op_width = is_data_16bit ? 2 : 4;
			use_imm = 1;
		} else if (fetch_data == 0xE9) {
			/* JUMP */
			op_kind = OP_JUMP;
			op_width = is_data_16bit ? 2 : 4;
			use_imm = 1;
			jmp_take = 1;
		} else if (fetch_data == 0xF4) {
			/* HLT */
			op_kind = OP_HLT;
		} else if (fetch_data == 0xF5) {
			/* CMC */
			op_kind = OP_CMC;
		} else if (0xF8 <= fetch_data && fetch_data <= 0xFD) {
			/* CLC/STC/CLI/STI/CLD/STD */
			op_kind = (fetch_data & 0x01) ? OP_SET_FLAG : OP_CLEAR_FLAG;
			switch (fetch_data & 0xFE) {
			case 0xF8: imm_value = CF; break;
			case 0xFA: imm_value = IF; break;
			case 0xFC: imm_value = DF; break;
			}
		} else if ((fetch_data & 0xFE) == 0xF6) {
			/* TEST/NOT/NEG/MUL/IMUL/DIV/IDIV */
			op_kind = OP_ARITHMETIC;
			op_arithmetic_kind = OP_READ_MODRM_MUL;
			op_width = (fetch_data & 0x01) ? (is_data_16bit ? 2 : 4) : 1;
			use_mod_rm = 1;
		} else if ((fetch_data & 0xFE) == 0xFE) {
			/* INC/DEC/CALL/CALLF/JMP/JMPF/PUSH */
			op_arithmetic_kind = OP_READ_MODRM_INC;
			op_width = (fetch_data & 0x01) ? (is_data_16bit ? 2 : 4) : 1;
			use_mod_rm = 1;
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

		if (op_arithmetic_kind == OP_READ_MODRM) {
			/* 「mod r/mを見て決定する」演算を決定する */
			static const int kind_table[] = {
				OP_ADD, OP_OR, OP_ADC, OP_SBB, OP_AND, OP_SUB, OP_XOR, OP_CMP
			};
			op_arithmetic_kind = kind_table[reg];
		} else if (op_arithmetic_kind == OP_READ_MODRM_MUL) {
			/* 「mod r/mを見て決定する」MUL系の演算を決定する */
			static const int op_table[] = {
				OP_ARITHMETIC, OP_ARITHMETIC, OP_NOT, OP_ARITHMETIC,
				OP_MUL, OP_IMUL, OP_DIV, OP_IDIV
			};
			op_kind = op_table[reg];
			if (reg <= 1) {
				op_arithmetic_kind = OP_TEST;
			} else if (reg == 3) {
				op_arithmetic_kind = OP_NEG;
			}
			if (reg <= 1) {
				use_imm = 1;
				src_kind = OP_KIND_IMM;
			}
			if (reg <= 3) need_dest_value = 1;
			if (4 <= reg) is_dest_reg = 1;
			if (reg == 4 || reg == 5) imul_store_upper = 1;
		} else if (op_arithmetic_kind == OP_READ_MODRM_INC) {
			/* 「mod r/mを見て決定する」INC系の演算を決定する */
			static const int op_table[] = {
				OP_INCDEC, OP_INCDEC,
				OP_CALL_ABSOLUTE, OP_CALL_FAR,
				OP_JUMP_ABSOLUTE, OP_JUMP_FAR,
				OP_PUSH, 0
			};
			op_kind = op_table[reg];
			if (reg <= 1) {
				imm_value = (reg == 0 ? 1 : -1);
				need_dest_value = 1;
			} else if (reg <= 6) {
				if (op_width == 1) {
					fprintf(stderr, "undefined operation reg=%d at %08"PRIx32"\n", reg, inst_addr);
					print_regs(stderr);
					return 0;
				}
				is_dest_reg = 1;
			} else {
				fprintf(stderr, "undefined operation reg=%d at %08"PRIx32"\n", reg, inst_addr);
				print_regs(stderr);
				return 0;
			}
		}
		if (op_shift_kind == OP_READ_MODRM_SHIFT) {
			/* 「mod r/mを見て決定する」シフト系の演算を決定する */
			static const int kind_table[] = {
				OP_ROL, OP_ROR, OP_RCL, OP_RCR, OP_SHL, OP_SHR, OP_SHL, OP_SAR
			};
			op_shift_kind = kind_table[reg];
		}
		if (op_kind == OP_FPU) {
			/* FPU系の演算を決定する */
			if (op_fpu_kind == 3 && reg == 4) {
				/* FNINIT/FINIT */
				/* 無視 */
			} else {
				fprintf(stderr, "FPU operations are unimplemented at %08"PRIx32"\n", inst_addr);
				print_regs(stderr);
				return 0;
			}
		}

		if (op_width == 1 && (op_kind != OP_MOVZX && op_kind != OP_MOVSX)) {
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
			modrm_is_mem = 1;
			if (is_addr_16bit) {
				/* 16-bit mod r/m */
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

		modrm_reg_index = r32;
		if (idx != 4) {
			modrm_reg2_index = idx;
			switch (ss) {
				case 0: modrm_reg2_scale = 1; break;
				case 1: modrm_reg2_scale = 2; break;
				case 2: modrm_reg2_scale = 4; break;
				case 3: modrm_reg2_scale = 8; break;
			}
		}
		if (r32 == 5 && disp_size == 0) {
			disp_size = 4;
			modrm_no_reg = 1;
		}
	}

	/* dispを解析する */
	uint32_t disp = 0;
	if (!use_mod_rm) disp_size = direct_disp_size;
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
		if (!modrm_disable_src) {
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
			if (op_kind == OP_IMUL && !imul_enable_dest) {
				dest_kind = OP_KIND_REG;
				dest_reg_index = EAX;
			} else {
				dest_kind = reg_kind;
				dest_reg_index = reg_index;
			}
		} else {
			dest_kind = modrm_kind;
			dest_reg_index = modrm_reg_index;
			dest_addr = modrm_addr;
		}
	} else if (direct_disp_size > 0) {
		uint32_t disp_value = disp;
		if (direct_disp_size < 4) disp_value &= UINT32_C(0xffffffff) >> (8 * (4 - direct_disp_size));
		if (is_dest_direct_disp) {
			dest_kind = OP_KIND_MEM;
			dest_addr = disp_value;
		} else {
			src_kind = OP_KIND_MEM;
			src_addr = disp_value;
		}
	}

	/* 即値を解析する */
	if (use_imm) {
		int imm_size = one_byte_imm ? 1 : op_width;
		imm_value = step_memread(&memread_ok, inst_addr, eip, imm_size);
		if (!memread_ok) return 0;
		eip += imm_size;
	}

	/* オペランドを読み込む */
	uint32_t src_value = 0;
	uint32_t dest_value = 0;

	switch (src_kind) {
	case OP_KIND_IMM:
		src_value = imm_value;
		break;
	case OP_KIND_MEM:
		if (op_kind == OP_LEA) {
			src_value = src_addr;
		} else {
			src_value = step_memread(&memread_ok, inst_addr, src_addr, op_width);
			if (!memread_ok) return 0;
		}
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
	uint32_t result = 0;
	int result_write = 0;

#define NOT_IMPLEMENTED(name) \
	fprintf(stderr, "operation " #name " not implemented at %08"PRIx32"\n", inst_addr); \
	print_regs(stderr); \
	return 0;

	switch (op_kind) {
	case OP_ARITHMETIC:
		{
			uint64_t result64 = 0;
			uint64_t mask = ((UINT64_C(1) << (op_width * 8)) - 1);
			uint64_t sign_mask = (UINT64_C(1) << (op_width * 8 - 1));
			uint32_t next_eflags = eflags & ~(OF | SF | ZF | AF | PF | CF);
			uint64_t src_masked = src_value & mask, dest_masked = dest_value & mask;
			int par = 0, i;
			result_write = 1;
			switch (op_arithmetic_kind) {
			case OP_ADD:
				result64 = dest_masked + src_masked;
				if ((dest_masked & sign_mask) == (src_masked & sign_mask) &&
				(result64 & sign_mask) != (dest_masked & sign_mask)) next_eflags |= OF;
				break;
			case OP_ADC:
				result64 = dest_masked + src_masked + (eflags & CF ? 1 : 0);
				if ((dest_masked & sign_mask) == (src_masked & sign_mask) &&
				(result64 & sign_mask) != (dest_masked & sign_mask)) next_eflags |= OF;
				break;
			case OP_SUB:
				result64 = dest_masked - src_masked;
				if ((dest_masked & sign_mask) == (-src_masked & sign_mask) &&
				(result64 & sign_mask) != (dest_masked & sign_mask)) next_eflags |= OF;
				break;
			case OP_SBB:
				result64 = dest_masked - src_masked - (eflags & CF ? 1 : 0);
				if ((dest_masked & sign_mask) == (-src_masked & sign_mask) &&
				(result64 & sign_mask) != (dest_masked & sign_mask)) next_eflags |= OF;
				break;
			case OP_AND:
				result64 = dest_masked & src_masked;
				break;
			case OP_OR:
				result64 = dest_masked | src_masked;
				break;
			case OP_XOR:
				result64 = dest_masked ^ src_masked;
				break;
			case OP_CMP:
				result64 = dest_masked - src_masked;
				if ((dest_masked & sign_mask) == (-src_masked & sign_mask) &&
				(result64 & sign_mask) != (dest_masked & sign_mask)) next_eflags |= OF;
				result_write = 0;
				break;
			case OP_TEST:
				result64 = dest_masked & src_masked;
				result_write = 0;
				break;
			case OP_NEG:
				result64 = -dest_masked;
				if ((dest_masked & sign_mask) == (result64 & sign_mask) && dest_masked != 0) next_eflags |= OF;
				break;
			default:
				fprintf(stderr, "unknown arithmethc %d at %08"PRIx32"\n", (int)op_arithmetic_kind, inst_addr);
				print_regs(stderr);
				return 0;
			}
			if (result64 & sign_mask) next_eflags |= SF;
			if ((result64 & mask) == 0) next_eflags |= ZF;
			if (result64 & (UINT64_C(1) << (op_width * 8))) next_eflags |= CF;
			for (i = 0; i < 8; i++) {
				if (result64 & (1 << i)) par++;
			}
			if (par % 2 == 0) next_eflags |= PF;
			result = (uint32_t)result64;
			eflags = next_eflags;
		}
		break;
	case OP_SHIFT:
		{
			uint32_t shift_width = src_value & 31;
			if (shift_width > 0) {
				uint32_t next_eflags = eflags;
				uint32_t sign_mask = UINT32_C(1) << (8 * op_width - 1);
				uint64_t upper_carry_mask = UINT64_C(1) << (8 * op_width);
				uint64_t value_mask = upper_carry_mask - 1;
				uint64_t result64;
				int enable_result_flags = 0;
				int carry = 0;
				switch (op_shift_kind) {
				case OP_ROL:
					result64 = dest_value & value_mask;
					result64 = (result64 << shift_width) | (result64 >> (8 * op_width - shift_width));
					carry = (result64 & upper_carry_mask) != 0;
					break;
				case OP_ROR:
					result64 = dest_value & value_mask;
					result64 = (result64 >> shift_width) | (result64 << (8 * op_width - shift_width));
					carry = ((dest_value >> (shift_width - 1)) & 1) != 0;
					break;
				case OP_RCL:
					result64 = dest_value & value_mask;
					if (next_eflags & CF) result64 |= upper_carry_mask;
					result64 = (result64 << shift_width) | (result64 >> (8 * op_width + 1 - shift_width));
					carry = (result64 & upper_carry_mask) != 0;
					break;
				case OP_RCR:
					result64 = dest_value & value_mask;
					if (next_eflags & CF) result64 |= upper_carry_mask;
					result64 = (result64 >> shift_width) | (result64 << (8 * op_width + 1 - shift_width));
					carry = (result64 & upper_carry_mask) != 0;
					break;
				case OP_SHL:
					result64 = (uint64_t)dest_value << shift_width;
					carry = (result64 & upper_carry_mask) != 0;
					enable_result_flags = 1;
					break;
				case OP_SHR:
					result64 = (uint64_t)dest_value >> shift_width;
					enable_result_flags = 1;
					break;
				case OP_SAR:
					result64 = dest_value;
					if (result64 & sign_mask) result64 |= UINT64_C(0xffffffff00000000);
					result64 = result64 >> shift_width;
					carry = ((dest_value >> (shift_width - 1)) & 1) != 0;
					enable_result_flags = 1;
					break;
				case OP_SHLD:
					shift_width = (use_imm ? imm_value : regs[ECX]) & 31;
					result64 = ((uint64_t)dest_value << shift_width) | ((src_value & value_mask) >> (8 * op_width - shift_width));
					carry = (result64 & upper_carry_mask) != 0;
					enable_result_flags = 1;
					break;
				case OP_SHRD:
					shift_width = (use_imm ? imm_value : regs[ECX]) & 31;
					result64 = ((dest_value & value_mask) >> shift_width) | ((uint64_t)src_value << (8 * op_width - shift_width));
					carry = ((dest_value >> (shift_width - 1)) & 1) != 0;
					enable_result_flags = 1;
					break;
				default:
					fprintf(stderr, "unknown shift %d at %08"PRIx32"\n", (int)op_shift_kind, inst_addr);
					print_regs(stderr);
					return 0;
				}
				result = (uint32_t)result64;
				result_write = 1;
				if (shift_width == 1) {
					if ((dest_value & sign_mask) == (result64 & sign_mask)) {
						next_eflags &= ~OF;
					} else {
						next_eflags |= OF;
					}
				}
				if (carry) next_eflags |= CF; else next_eflags &= ~CF;
				if (enable_result_flags) {
					int i, par = 0;
					for (i = 0; i < 8; i++) {
						if ((result64 >> i) & 1) par++;
					}
					if (par % 2 == 0) next_eflags |= PF; else next_eflags &= ~PF;
					if ((result64 & value_mask) == 0) next_eflags |= ZF; else next_eflags &= ~ZF;
					if (result64 & sign_mask) next_eflags |= SF; else next_eflags &= ~SF;
				}
				eflags = next_eflags;
			}
		}
		break;
	case OP_XCHG:
		result = src_value;
		result_write = 1;
		switch (src_kind) {
		case OP_KIND_IMM:
			fprintf(stderr, "tried to write into immediate value at %08"PRIx32"\n", inst_addr);
			print_regs(stderr);
			return 0;
		case OP_KIND_MEM:
			if (!step_memwrite(inst_addr, src_addr, dest_value, op_width)) return 0;
			break;
		case OP_KIND_REG:
			{
				uint32_t mask = (op_width >= 4 ? 0 : UINT32_C(0xffffffff) << (op_width * 8));
				regs[src_reg_index] = (regs[src_reg_index] & mask) | (dest_value & ~mask);
			}
			break;
		case OP_KIND_REG_HIGH8:
			if (op_width != 1) {
				fprintf(stderr, "tried to write high register with width other than 1 at %08"PRIx32"\n", inst_addr);
				print_regs(stderr);
				return 0;
			}
			regs[src_reg_index] = (regs[src_reg_index] & UINT32_C(0xffff00ff)) | ((dest_value & 0xff) << 8);
			break;
		}
		break;
	case OP_MOV:
		result = src_value;
		result_write = 1;
		break;
	case OP_MOVZX:
		result = src_value;
		if (op_width < 4) result &= UINT32_C(0xffffffff) >> (8 * (4 - op_width));
		result_write = 1;
		op_width = is_data_16bit ? 2 : 4;
		break;
	case OP_MOVSX:
		result = src_value;
		result_write = 1;
		op_width = is_data_16bit ? 2 : 4;
		break;
	case OP_SETCC:
		result = jmp_take ? 1 : 0;
		result_write = 1;
		break;
	case OP_LEA:
		result = src_value;
		result_write = 1;
		break;
	case OP_INCDEC:
		{
			uint32_t sign_mask = (UINT32_C(1) << (op_width * 8 - 1));
			uint32_t next_eflags = eflags & ~(OF | SF | ZF | AF | PF);
			result = dest_value + imm_value;
			result_write = 1;
			if ((dest_value & sign_mask) == (src_value & sign_mask) &&
			(result & sign_mask) != (dest_value & sign_mask)) next_eflags |= OF;
			if (result & sign_mask) next_eflags |= SF;
			if ((result & ((UINT64_C(1) << (op_width * 8)) - 1)) == 0) next_eflags |= ZF;
			eflags = next_eflags;
		}
		break;
	case OP_NOT:
		result = ~dest_value;
		result_write = 1;
		break;
	case OP_MUL:
		NOT_IMPLEMENTED(OP_MUL)
		break;
	case OP_IMUL:
		{
			uint32_t mask = op_width == 4 ? UINT32_C(0xffffffff) : UINT32_C(0xffffffff) >> (8 * (4 - op_width));
			uint32_t sign_mask = UINT32_C(1) << (8 * op_width - 1);
			uint64_t s = src_value;
			uint64_t d = (use_imm ? imm_value : dest_value);
			uint32_t upper;
			if (s & sign_mask) s |= UINT64_C(0xffffffff00000000);
			if (d & sign_mask) d |= UINT64_C(0xffffffff00000000);
			d *= s;
			upper = (uint32_t)(d >> (8 * op_width));
			result = (uint32_t)d;
			result_write = 1;
			if (imul_store_upper) {
				if (op_width == 1) regs[EAX] = (regs[EAX] & UINT32_C(0xffff00ff)) | ((upper & 0xff) << 8);
				else if (op_width == 2) regs[EDX] = (regs[EDX] & UINT32_C(0xffff0000)) | (upper & 0xffff);
				else regs[EDX] = upper;
			}
			if ((upper & mask) == ((result & sign_mask) ? mask : 0)) {
				eflags |= (OF | CF);
			} else {
				eflags &= ~(OF | CF);
			}
		}
		break;
	case OP_DIV:
		{
			uint64_t d;
			uint32_t mask = op_width == 4 ? UINT32_C(0xffffffff) : UINT32_C(0xffffffff) >> (8 * (4 - op_width));
			uint32_t s = src_value & mask;
			uint32_t rem;
			if (op_width == 1) d = regs[EAX] & 0xffff;
			else if (op_width == 2) d = ((regs[EDX] & 0xffff) << 16) | (regs[EAX] & 0xffff);
			else d = ((uint64_t)regs[EDX] << 32) | regs[EAX];
			if (s == 0) {
				fprintf(stderr, "divide by zero at %08"PRIx32"\n", inst_addr);
				print_regs(stderr);
				return 0;
			}
			rem = (uint32_t)(d % s);
			d = d / s;
			if (d > mask) {
				fprintf(stderr, "result of division too large at %08"PRIx32"\n", inst_addr);
				print_regs(stderr);
				return 0;
			}
			if (op_width == 1) {
				regs[EAX] = (regs[EAX] & UINT32_C(0xffff0000)) |
					((rem & 0xff) << 8) | (uint32_t)(d & 0xff);
			} else if (op_width == 2) {
				regs[EAX] = (regs[EAX] & UINT32_C(0xffff0000)) | (uint32_t)(d & 0xffff);
				regs[EDX] = (regs[EDX] & UINT32_C(0xffff0000)) | (rem & 0xffff);
			} else {
				regs[EAX] = (uint32_t)d;
				regs[EDX] = rem;
			}
		}
		break;
	case OP_IDIV:
		NOT_IMPLEMENTED(OP_IDIV)
		break;
	case OP_PUSH:
		if (!step_push(inst_addr, src_value, op_width, is_addr_16bit)) return 0;
		break;
	case OP_POP:
		result = step_pop(&memread_ok, inst_addr, op_width, is_addr_16bit);
		if (!memread_ok) return 0;
		result_write = 1;
		break;
	case OP_PUSHA:
		NOT_IMPLEMENTED(OP_PUSHA)
		break;
	case OP_POPA:
		NOT_IMPLEMENTED(OP_POPA)
		break;
	case OP_PUSHF:
		if (!step_push(inst_addr, eflags & UINT32_C(0x00fcffff), op_width, is_addr_16bit)) return 0;
		break;
	case OP_POPF:
		{
			uint32_t new_eflags = step_pop(&memread_ok, inst_addr, op_width, is_addr_16bit);
			if (!memread_ok) return 0;
			if (is_data_16bit) {
				eflags = (eflags & UINT32_C(0xffff0000)) | (new_eflags & 0xffff);
			} else {
				eflags = (eflags & UINT32_C(0x001a0000)) | (new_eflags & UINT32_C(0xff24ffff));
			}
		}
		break;
	case OP_STRING:
		{
			int enable_zf = (op_string_kind == OP_STR_CMP || op_string_kind == OP_STR_SCA);
			int enable_esi = (op_string_kind == OP_STR_MOV || op_string_kind == OP_STR_CMP ||
				op_string_kind == OP_STR_LOD || op_string_kind == OP_STR_OUT);
			int enable_edi = (op_string_kind == OP_STR_MOV || op_string_kind == OP_STR_CMP ||
				op_string_kind == OP_STR_STO || op_string_kind == OP_STR_SCA || op_string_kind == OP_STR_IN);
			uint64_t sign_mask = (UINT64_C(1) << (op_width * 8 - 1));
			uint32_t new_eflags = eflags;
			int zero = 0;
			uint32_t delta = (eflags & DF) ? -op_width : op_width;
			if (op_string_kind == OP_STR_LOD) result_write = 1;
			do {
				uint32_t esi_addr = is_addr_16bit ? regs[ESI] & 0xffff : regs[ESI];
				uint32_t edi_addr = is_addr_16bit ? regs[EDI] & 0xffff : regs[EDI];
				uint32_t s =0 , d = 0;
				switch (op_string_kind) {
				case OP_STR_MOV:
					s = step_memread(&memread_ok, inst_addr, esi_addr, op_width);
					if (!memread_ok) return 0;
					if (!step_memwrite(inst_addr, edi_addr, s, op_width)) return 0;
					break;
				case OP_STR_CMP:
					s = step_memread(&memread_ok, inst_addr, esi_addr, op_width);
					if (!memread_ok) return 0;
					d = step_memread(&memread_ok, inst_addr, edi_addr, op_width);
					if (!memread_ok) return 0;
					break;
				case OP_STR_STO:
					if (!step_memwrite(inst_addr, edi_addr, src_value, op_width)) return 0;
					break;
				case OP_STR_LOD:
					result = step_memread(&memread_ok, inst_addr, esi_addr, op_width);
					if (!memread_ok) return 0;
					break;
				case OP_STR_SCA:
					s = src_value;
					d = step_memread(&memread_ok, inst_addr, edi_addr, op_width);
					if (!memread_ok) return 0;
					break;
				/*
				case OP_STR_IN:
					break;
				case OP_STR_OUT:
					break;
				*/
				default:
					fprintf(stderr, "unknown string operation %d at %08"PRIx32"\n", (int)op_string_kind, inst_addr);
					print_regs(stderr);
					return 0;
				}
				if (op_string_kind == OP_STR_CMP || op_string_kind == OP_STR_SCA) {
					uint32_t next_eflags = new_eflags & ~(OF | SF | ZF | AF | PF | CF);
					uint64_t res = (uint64_t)s - (uint64_t)d;
					int i, par;
					if ((s & sign_mask) == (-d & sign_mask) &&
					(res & sign_mask) != (s & sign_mask)) next_eflags |= OF;
					if (res & sign_mask) next_eflags |= SF;
					if ((res & ((UINT64_C(1) << (op_width * 8)) - 1)) == 0) next_eflags |= ZF;
					if (res & (UINT64_C(1) << (op_width * 8))) next_eflags |= CF;
					par = 0;
					for (i = 0; i < 8; i++) {
						if (res & (1 << i)) par++;
					}
					if (par % 2 == 0) next_eflags |= PF;
					new_eflags = next_eflags;
					zero = (new_eflags & ZF);
				}
				if (is_addr_16bit) {
					if (enable_esi) {
						regs[ESI] = (regs[ESI] & UINT32_C(0xffff0000)) | ((regs[ESI] + delta) & 0xffff);
					}
					if (enable_edi) {
						regs[EDI] = (regs[EDI] & UINT32_C(0xffff0000)) | ((regs[EDI] + delta) & 0xffff);
					}
					if (is_rep) {
						regs[ECX] = (regs[ECX] & UINT32_C(0xffff0000)) | ((regs[ECX] - 1) & 0xffff);
					}
				} else {
					if (enable_esi) regs[ESI] += delta;
					if (enable_edi) regs[EDI] += delta;
					if (is_rep) regs[ECX] -= 1;
				}
			} while (is_rep && (is_addr_16bit ? regs[ECX] & 0xffff : regs[ECX]) != 0 &&
			(!enable_zf || (is_rep_while_zero ? zero : !zero)));
			if (enable_zf) {
				eflags = new_eflags;
			}
		}
		break;
	case OP_CALL:
		if (!step_push(inst_addr, eip, op_width, is_addr_16bit)) return 0;
		eip += src_value;
		if (is_data_16bit) eip &= 0xffff;
		break;
	case OP_JUMP:
		if (jmp_take) eip += src_value;
		break;
	case OP_CALL_ABSOLUTE:
		if (!step_push(inst_addr, eip, op_width, is_addr_16bit)) return 0;
		eip = src_value;
		if (is_data_16bit) eip &= 0xffff;
		break;
	case OP_JUMP_ABSOLUTE:
		eip = src_value;
		if (is_data_16bit) eip &= 0xffff;
		break;
	case OP_CALL_FAR:
		NOT_IMPLEMENTED(OP_CALL_FAR)
		break;
	case OP_JUMP_FAR:
		NOT_IMPLEMENTED(OP_JUMP_FAR)
		break;
	case OP_CBW:
		if (op_width == 2) {
			result = src_value & 0xff;
			if (result & 0x80) result |= 0xff00;
		} else {
			result = src_value & 0xffff;
			if (result & 0x8000) result |= UINT32_C(0xffff0000);
		}
		result_write = 1;
		break;
	case OP_CWD:
		if (op_width == 2) {
			result = src_value & 0x8000 ? 0xffff : 0x0000;
		} else {
			result = src_value & UINT32_C(0x80000000) ? UINT32_C(0xffffffff) : UINT32_C(0x00000000);
		}
		result_write = 1;
		break;
	case OP_SAHF:
		NOT_IMPLEMENTED(OP_SAHF)
		break;
	case OP_LAHF:
		NOT_IMPLEMENTED(OP_LAHF)
		break;
	case OP_RETN:
		{
			uint32_t next_eip = step_pop(&memread_ok, inst_addr, is_data_16bit ? 2 : 4, is_addr_16bit);
			if (!memread_ok) return 0;
			eip = next_eip;
		}
		break;
	case OP_LEAVE:
		{
			uint32_t next_ebp = 0;
			if (is_addr_16bit) {
				regs[ESP] = (regs[ESP] & UINT32_C(0xffff0000)) | (regs[EBP] & 0xffff);
			} else {
				regs[ESP] = regs[EBP];
			}
			next_ebp = step_pop(&memread_ok, inst_addr, op_width, is_addr_16bit);
			if (!memread_ok) return 0;
			if (op_width == 2) {
				regs[EBP] = (regs[EBP] & UINT32_C(0xffff0000)) | (next_ebp & 0xffff);
			} else {
				regs[EBP] = next_ebp;
			}
		}
		break;
	case OP_INT:
		if (use_xv6_syscall && src_value == 0x40) {
			int sysret = xv6_syscall(regs);
			if (sysret == 0) return 0;
			else if (sysret < 0) {
				print_regs(stderr);
				return 0;
			}
		} else {
			NOT_IMPLEMENTED(OP_INT)
		}
		break;
	case OP_INTO:
		NOT_IMPLEMENTED(OP_INTO)
		break;
	case OP_IRET:
		NOT_IMPLEMENTED(OP_IRET)
		break;
	case OP_LOOP:
		result = src_value - 1;
		result_write = 1;
		if (jmp_take && src_value != 1) eip += src_value;
		break;
	case OP_IN:
		NOT_IMPLEMENTED(OP_IN)
		break;
	case OP_OUT:
		NOT_IMPLEMENTED(OP_OUT)
		break;
	case OP_HLT:
		NOT_IMPLEMENTED(OP_HLT)
		break;
	case OP_CMC:
		eflags ^= CF;
		break;
	case OP_SET_FLAG:
		eflags |= imm_value;
		break;
	case OP_CLEAR_FLAG:
		eflags &= ~imm_value;
		break;
	case OP_FPU:
		/* 無視(無視しない命令はデコード時に弾いているので) */
		break;
	default:
		fprintf(stderr, "unknown operation kind %d at %08"PRIx32"\n", (int)op_kind, inst_addr);
		print_regs(stderr);
		return 0;
	}

	/* 計算結果を書き込む */
	if (result_write) {
		switch (dest_kind) {
		case OP_KIND_IMM:
			fprintf(stderr, "tried to write into immediate value at %08"PRIx32"\n", inst_addr);
			print_regs(stderr);
			return 0;
		case OP_KIND_MEM:
			if (!step_memwrite(inst_addr, dest_addr, result, op_width)) return 0;
			break;
		case OP_KIND_REG:
			{
				uint32_t mask = (op_width >= 4 ? 0 : UINT32_C(0xffffffff) << (op_width * 8));
				regs[dest_reg_index] = (regs[dest_reg_index] & mask) | (result & ~mask);
			}
			break;
		case OP_KIND_REG_HIGH8:
			if (op_width != 1) {
				fprintf(stderr, "tried to write high register with width other than 1 at %08"PRIx32"\n", inst_addr);
				print_regs(stderr);
				return 0;
			}
			regs[dest_reg_index] = (regs[dest_reg_index] & UINT32_C(0xffff00ff)) | ((result & 0xff) << 8);
			break;
		}
	}

	/* フラグIDを0に固定し、CPUID命令が無いことを示す */
	eflags &= ~UINT32_C(0x00200000);

	return 1;
}

int str_to_uint32(uint32_t* out, const char* str) {
	uint32_t value = 0;
	uint32_t digit_mult = 0;
	if (str[0] == '0') {
		if (str[1] == 'x' || str[1] == 'X') {
			digit_mult = 16;
			str += 2;
		} else if (str[1] == 'b' || str[1]== 'B') {
			digit_mult = 2;
			str += 2;
		} else if (str[1] == '\0') {
			*out = 0;
			return 1;
		} else {
			digit_mult = 8;
			str += 1;
		}
	} else {
		digit_mult = 10;
	}
	while (*str != '\0') {
		uint32_t digit_value = 0;
		if ('0' <= *str && *str <= '9') digit_value = *str - '0';
		else if ('a' <= *str && *str <= 'z') digit_value = *str - 'a' + 10;
		else if ('A' <= *str && *str <= 'Z') digit_value = *str - 'A' + 10;
		else return 0; /* 不正な文字 */
		if (digit_value >= digit_mult) return 0; /* 進数に対して大きすぎる数字 */
		if (UINT32_MAX / digit_mult < value) return 0; /* オーバーフロー */
		value *= digit_mult;
		if (UINT32_MAX - digit_value < value) return 0; /* オーバーフロー */
		value += digit_value;
		str++;
	}
	*out = value;
	return 1;
}

int main(int argc, char *argv[]) {
	int i;
	int enable_trace = 0;
	int enable_args = 0;
	int import_as_iat = 0;
	uint32_t initial_eip = 0;
	uint32_t initial_esp = UINT32_C(0xfffff000);
	uint32_t stack_size = 4096;
	uint32_t xv6_syscall_work = UINT32_C(0x80000000);
	uint32_t pe_import_work = UINT32_C(0x80000000);
	uint32_t argc2 = 0, argv_addr = 0;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--raw") == 0) {
			if (++i < argc) { if (!read_raw(argv[i])) return 1; }
			else { fprintf(stderr, "no filename for --raw\n"); return 1; }
		} else if (strcmp(argv[i], "--elf") == 0) {
			if (++i < argc) { if (!read_elf(&initial_eip, argv[i])) return 1; }
			else { fprintf(stderr, "no filename for --elf\n"); return 1; }
		} else if (strcmp(argv[i], "--pe") == 0) {
			if (++i < argc) { if (!read_pe(&initial_eip, &stack_size, &import_params, argv[i])) return 1; }
			else { fprintf(stderr, "no filename for --pe\n"); return 1; }
		} else if (strcmp(argv[i], "--trace") == 0) {
			enable_trace = 1;
		} else if (strcmp(argv[i], "--eip") == 0) {
			if (++i < argc) {
				if (!str_to_uint32(&initial_eip, argv[i])) {
					fprintf(stderr, "invalid initial eip value %s\n", argv[i]);
					return 1;
				}
			} else { fprintf(stderr, "no eip value for --eip\n"); return 1;}
		} else if (strcmp(argv[i], "--esp") == 0) {
			if (++i < argc) {
				if (!str_to_uint32(&initial_esp, argv[i])) {
					fprintf(stderr, "invalid initial esp value %s\n", argv[i]);
					return 1;
				}
			} else { fprintf(stderr, "no esp value for --esp\n"); return 1;}
		} else if (strcmp(argv[i], "--stacksize") == 0) {
			if (++i < argc) {
				if (!str_to_uint32(&stack_size, argv[i])) {
					fprintf(stderr, "invalid stack size %s\n", argv[i]);
					return 1;
				}
			} else { fprintf(stderr, "no stack size for --stacksize\n"); return 1;}
		} else if (strcmp(argv[i], "--args") == 0) {
			i++;
			enable_args = 1;
			break;
		} else if (strcmp(argv[i], "--xv6-syscall") == 0) {
			use_xv6_syscall = 1;
			if (++i < argc) {
				if (!str_to_uint32(&xv6_syscall_work, argv[i])) {
					fprintf(stderr, "invalid xv6 system call work buffer origin %s\n", argv[i]);
					return 1;
				}
			} else { fprintf(stderr, "no work buffer origin for --xv6-syscall\n"); return 1;}
		} else if (strcmp(argv[i], "--pe-import") == 0) {
			use_pe_import = 1;
			if (++i < argc) {
				if (!str_to_uint32(&pe_import_work, argv[i])) {
					fprintf(stderr, "invalid PE import libs work buffer origin %s\n", argv[i]);
					return 1;
				}
			} else { fprintf(stderr, "no work buffer origin for --pe-import\n"); return 1;}
		} else if (strcmp(argv[i], "--pe-import-as-iat") == 0) {
			import_as_iat = 1;
		} else {
			fprintf(stderr, "unknown command line option %s\n", argv[i]);
			return 1;
		}
	}
	if (stack_size > initial_esp) {
		fprintf(stderr, "stack too big compared to esp\n");
		return 1;
	}

	eip = initial_eip;
	eflags = UINT32_C(0x00000002);
	regs[ESP] = initial_esp;
	dmemory_allocate(initial_esp - stack_size, stack_size);
	if (enable_args) {
		uint32_t j;
		char** argv2 = argv + i;
		uint32_t stack_limit = initial_esp - stack_size;
		uint32_t current_addr = initial_esp;
		uint32_t num_buffer = 0;
		argc2 = argc - i;
		/* argvが指す配列の領域を確保する */
		if (argc2 == UINT32_MAX || UINT32_MAX / 4 < (argc2 + 1)) {
			fprintf(stderr, "too many arguments\n");
			return 1;
		}
		if (current_addr - stack_limit < 4 * (argc2 + 1)) {
			fprintf(stderr, "stack too small to hold argv table\n");
			return 1;
		}
		current_addr -= 4 * (argc2 + 1);
		argv_addr = current_addr;
		/* 引数の文字列とargvが指す配列の値を書き込む */
		for (j = 0; j < argc2; j++) {
			uint32_t stack_left = current_addr - stack_limit;
			size_t alen = strlen(argv2[j]);
			if (stack_left == 0 || stack_left - 1 < alen) {
				fprintf(stderr, "stack too small to hold argv[%"PRIu32"]\n", j);
				return 1;
			}
			current_addr -= alen + 1;
			dmemory_write(argv2[j], current_addr, alen + 1);
			dmemory_write(&current_addr, argv_addr + j * 4, sizeof(current_addr));
		}
		dmemory_write(&num_buffer, argv_addr + argc2 * 4, sizeof(num_buffer));
		if (current_addr - stack_limit < 12) {
			fprintf(stderr, "stack too small to hold arguments\n");
			return 1;
		}
		/* main関数に渡す引数とダミーのリターンアドレスを書き込む */
		current_addr -= 12;
		dmemory_write(&argv_addr, current_addr + 8, sizeof(argv_addr));
		dmemory_write(&argc2, current_addr + 4, sizeof(argc2));
		num_buffer = UINT32_C(0xfffffff0);
		dmemory_write(&num_buffer, current_addr, sizeof(num_buffer));
		regs[ESP] = current_addr;
	}
	if (import_as_iat) {
		import_params.iat_addr = import_params.import_addr;
		import_params.iat_size = import_params.import_size;
	}
	if (use_xv6_syscall) {
		if (!initialize_xv6_syscall(xv6_syscall_work)) return 1;
	}
	if (use_pe_import) {
		if (!pe_import_initialize(&import_params, pe_import_work, argc2, argv_addr)) return 1;
	}

	if (enable_trace) {
		print_regs(stdout);
		putchar('\n');
	}
	while(step()) {
		if (enable_trace) {
			print_regs(stdout);
			putchar('\n');
		}
	}
	return 0;
}
