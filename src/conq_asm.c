#include "log.h"
#include "conq_asm.h"

#include <assert.h>
#include <stdlib.h>

static bool stringEqN0(conq_BufConst s1, const char *s2);
static bool isWhitespace(uint8_t c);

static conq_BufConst readWord(conq_Asm *asm);
static bool readReg(conq_Asm *asm, uint8_t *dest);
static bool readInt(conq_Asm *asm, uint32_t *dest);

static void conq_Asm_write(conq_Asm *asm, uint8_t byte);

static const conq_BufConst ERRW_EOF = (conq_BufConst) { .ptr = (const uint8_t *) "EOF", .len = 3 };

#define GETBYTE_REG(asm, regname) \
	uint8_t regname; \
	if (!readReg(asm, &regname)) return false;

#define READ_INT(asm, ptr) \
	if (!readInt(asm, ptr)) return false;

#define TWO_ARG_ASM(sym) \
	if (stringEqN0(w, #sym)) { \
		GETBYTE_REG(asm, rsrc); \
		GETBYTE_REG(asm, rdest); \
		conq_Asm_write(asm, CONQ_INS_ ##sym); \
		conq_Asm_write(asm, ((rsrc & 0b111) << 5) | ((rdest & 0b111) << 2)); \
	}

conq_Asm conq_Asm_init(conq_BufConst src) {
	return (conq_Asm) {
		.src = src,
		.src_line = 1,
		.src_i = 0,
		.rom_i = 0,
	};
}

bool conq_Asm_compile(conq_Asm *asm, conq_Buf *dest_rom) {
	const size_t CHUNK_SIZE = 256;
	asm->rom = (conq_Buf) { .ptr = NULL, .len = CHUNK_SIZE };
	asm->rom.ptr = malloc(asm->rom.len * sizeof(uint8_t));

	while (true) {
		conq_BufConst w = readWord(asm);
		if (w.ptr == NULL) break;

		if (asm->rom_i >= asm->rom.len) {
			asm->rom.len += CHUNK_SIZE;
			asm->rom.ptr = realloc(asm->rom.ptr, asm->rom.len);
			if (asm->rom.ptr == NULL) {
				logD("OOM");
				return false;
			}
		}

		if (stringEqN0(w, "BRK")) {
			conq_Asm_write(asm, CONQ_INS_BRK);
		} else if (stringEqN0(w, "/*")) {
			while (true) {
				w = readWord(asm);
				if (w.ptr == NULL) {
					logD("missing */");
					return false;
				}
				if (stringEqN0(w, "*/")) {
					break;
				}
			}
		} else if (stringEqN0(w, "CPY")) {
			GETBYTE_REG(asm, rdest);
			GETBYTE_REG(asm, rval);

			conq_Asm_write(asm, CONQ_INS_CPY);
			conq_Asm_write(asm, ((rdest & 0b111) << 5) | ((rval & 0b111) << 2));
		} else if (stringEqN0(w, "LD8")) {
			GETBYTE_REG(asm, reg);

			uint32_t n;
			READ_INT(asm, &n);

			conq_Asm_write(asm, CONQ_INS_LD8);
			conq_Asm_write(asm, (reg & 0b111) << 5);
			conq_Asm_write(asm, (uint8_t) n);
		} else if (stringEqN0(w, "LD16")) {
			GETBYTE_REG(asm, reg);

			uint32_t n;
			READ_INT(asm, &n);

			conq_Asm_write(asm, CONQ_INS_LD16);
			conq_Asm_write(asm, (reg & 0b111) << 5);
			conq_Asm_write(asm, (uint8_t) (n >> 8));
			conq_Asm_write(asm, (uint8_t) n);
		} else if (stringEqN0(w, "LD32")) {
			GETBYTE_REG(asm, reg);

			uint32_t n;
			READ_INT(asm, &n);

			conq_Asm_write(asm, CONQ_INS_LD32);
			conq_Asm_write(asm, (reg & 0b111) << 5);
			conq_Asm_write(asm, (uint8_t) (n >> 24));
			conq_Asm_write(asm, (uint8_t) (n >> 16));
			conq_Asm_write(asm, (uint8_t) (n >> 8));
			conq_Asm_write(asm, (uint8_t) n);
		}
		else if (stringEqN0(w, "PRINT")) {
			GETBYTE_REG(asm, reg);

			conq_Asm_write(asm, CONQ_INS_PRINT);
			conq_Asm_write(asm, (reg & 0b111) << 5);
		}
		else if (stringEqN0(w, "NOT")) {
			conq_Asm_write(asm, CONQ_INS_NOT);
		}

		/* pointers */
		else TWO_ARG_ASM(WR8)
		else TWO_ARG_ASM(WR16)
		else TWO_ARG_ASM(WR32)
		else TWO_ARG_ASM(RD8)
		else TWO_ARG_ASM(RD16)
		else TWO_ARG_ASM(RD32)

		/* arithmetics */
		else TWO_ARG_ASM(ADD)
		else TWO_ARG_ASM(SUB)
		else TWO_ARG_ASM(DIV)
		else TWO_ARG_ASM(MUL)
		else TWO_ARG_ASM(SHL)
		else TWO_ARG_ASM(SHR)

		/* compare */
		else TWO_ARG_ASM(EQ)
		else TWO_ARG_ASM(NEQ)
		else TWO_ARG_ASM(LT)
		else TWO_ARG_ASM(LEQ)
		else TWO_ARG_ASM(GT)
		else TWO_ARG_ASM(GEQ)

		else {
			logD("line %d: unknown word: %.*s", asm->src_line, w.len, w.ptr);
			return false;
		}
	}

	/* try to tighten memory but it's not critical, as `len` is tight already. */
	uint8_t *tighter = realloc(asm->rom.ptr, asm->rom.len);
	if (tighter != NULL) asm->rom.ptr = tighter;

	*dest_rom = asm->rom;
	return true;
}

static conq_BufConst readWord(conq_Asm *asm) {
	/* skip whitespace and count lines */
	for (; asm->src_i < asm->src.len; asm->src_i += 1) {
		char c = asm->src.ptr[asm->src_i];
		if (c == '\n') asm->src_line += 1;
		if (isWhitespace(c)) continue;
		break;
	}

	/* count word chars */
	size_t start = asm->src_i;
	for (; !isWhitespace(asm->src.ptr[asm->src_i]) && asm->src_i < asm->src.len;
	     asm->src_i += 1)
		;

	return (start == asm->src_i)
		   ? (conq_BufConst) { .ptr = NULL, .len = 0 }
		   : (conq_BufConst) { .ptr = &asm->src.ptr[start], .len = asm->src_i - start };
}

static bool stringEqN0(conq_BufConst s1, const char *s2) {
	size_t i = 0;
	while (true) {
		if (i >= s1.len && s2[i] == '\0') return true; /* both ended */
		if (i >= s1.len || s2[i] == '\0') return false; /* only one ended */
		if (s1.ptr[i] != s2[i]) return false; /* different chars */
		i++;
	}
}

static bool isWhitespace(uint8_t c) {
	return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static bool readReg(conq_Asm *asm, uint8_t *dest) {
	conq_BufConst w = readWord(asm);
	if (w.ptr == NULL) {
		w = ERRW_EOF;
		goto error;
	}

	if (w.len != 2 || w.ptr[0] != 'r' || w.ptr[1] < '0' || w.ptr[1] > '7') goto error;

	*dest = w.ptr[1] - '0';
	assert((*dest & 0b111) == *dest);
	return true;

error:
	logD("expected register (r0-r7), found %.*s", w.len, w.ptr);
	return false;
}

static bool readInt(conq_Asm *asm, uint32_t *dest) {
	conq_BufConst w = readWord(asm);
	if (w.ptr == NULL) {
		w = ERRW_EOF;
		goto error;
	}

	if (w.len == 0) goto error;
	bool is_hex = (w.ptr[0] == '#');

	uint32_t mul = 1;
	uint32_t result = 0;
	size_t i = 0;
	for (; i < w.len; i++) {
		size_t idx = w.len - 1 - i;
		if (is_hex && idx == 0) continue;
		char c = w.ptr[idx];

		if (is_hex) {
			if (c >= '0' && c <= '9') {
				result += (uint32_t) (c - '0') * mul;
			} else if (c >= 'A' && c <= 'F') {
				result += (uint32_t) (c - 'A' + 10) * mul;
			} else if (c >= 'a' && c <= 'f') {
				result += (uint32_t) (c - 'a' + 10) * mul;
			} else {
				goto error;
			}
			mul *= 16;
		} else {
			if (c < '0' || c > '9') goto error;
			result += (uint32_t) (c - '0') * mul;
			mul *= 10;
		}
	}

	*dest = result;
	return true;

error:
	logD("expected integer, got \"%.*s\"", w.len, w.ptr);
	return false;
}

static void conq_Asm_write(conq_Asm *asm, uint8_t byte) {
	asm->rom.ptr[asm->rom_i] = byte;
	asm->rom_i++;
}
