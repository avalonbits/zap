/*
 * Host tests for instruction encoding.
 *
 * The reference's Opcodes corpus is the real coverage here -- all ten of its
 * files, roughly a thousand instructions including the undocumented set,
 * assemble byte for byte. What is worth having as a fast unit test is the
 * handful of cases where the encoding depends on something subtle, because
 * those are the ones a refactor breaks without the corpus noticing which
 * change did it.
 *
 * Expected bytes come from ez80asm.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "parser.h"

static int failures = 0;

static const char* emit(const char* src) {
    static char out[1024];
    char path[] = "/tmp/zap_enc_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        return "<mkstemp failed>";
    }
    if (write(fd, src, strlen(src)) != (long) strlen(src)) {
        close(fd);
        unlink(path);

        return "<write failed>";
    }
    close(fd);

    parser p;
    if (pr_init(&p, path) == NULL) {
        unlink(path);

        return "<init failed>";
    }

    const char* err = pr_parse(&p);
    int n = 0;
    out[0] = 0;
    if (err != NULL && *err != 0) {
        n = snprintf(out, sizeof(out), "ERR");
    } else {
        int sz = 0;
        const uint8_t* buf = pr_buf(&p, &sz);
        for (int i = 0; i < sz && n < (int) sizeof(out) - 4; i++) {
            n += snprintf(&out[n], sizeof(out) - n, "%s%02X", i ? " " : "", buf[i]);
        }
    }
    out[n] = 0;

    pr_destroy(&p);
    unlink(path);

    return out;
}

/* Assembles one instruction, in ADL mode unless the source says otherwise. */
static void insn_is(const char* insn, const char* want) {
    char src[256];
    snprintf(src, sizeof(src), "    %s\n", insn);
    const char* got = emit(src);
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-32s %s\n", insn, got);
    } else {
        fprintf(stderr, "FAIL  %-32s got %s, want %s\n", insn, got, want);
        failures++;
    }
}

int main(void) {
    /* The plain register forms, which the Y and Z transforms build. */
    insn_is("ld a,b", "78");
    insn_is("ld b,a", "47");
    insn_is("ld a,(hl)", "7E");
    insn_is("ld (hl),a", "77");
    insn_is("add a,b", "80");
    insn_is("or a", "B7");
    insn_is("ret", "C9");
    insn_is("ret z", "C8");
    insn_is("nop", "00");

    /* Register C is both a register and the carry condition. Which one it is
     * depends on the row that matches, so both readings have to work. */
    insn_is("ld a,c", "79");
    insn_is("ret c", "D8");
    insn_is("jp c,$40000", "DA 00 00 04");

    /* Index registers: IX and IY encode as HL, told apart by the DD/FD
     * prefix. */
    insn_is("ld a,(ix+5)", "DD 7E 05");
    insn_is("ld a,(iy-5)", "FD 7E FB");
    insn_is("ld (ix+5),a", "DD 77 05");
    insn_is("ld ixh,5", "DD 26 05");
    insn_is("ld a,iyl", "FD 7D");

    /* LEA and PEA write a displacement with no parentheses, because they
     * compute an address rather than dereference one. */
    insn_is("lea ix,iy+5", "ED 54 05");
    insn_is("pea ix+5", "ED 65 05");

    /* DDCB: the displacement sits between the CB prefix and the opcode, not
     * after it. And an index register written with a displacement contributes
     * no register index, or the register field here would pick up IX's 2. */
    insn_is("rlc (ix+0)", "DD CB 00 06");
    insn_is("bit 0,(ix+0)", "DD CB 00 46");

    /* The undocumented three-operand forms, which are spelled in the table as
     * res0..res7 and set0..set7. */
    insn_is(".cpu z80\n    res 0,(ix+0),b", "DD CB 00 80");
    insn_is(".cpu z80\n    set 7,(iy+1),a", "FD CB 01 FF");
    insn_is(".cpu z80\n    sll b", "CB 30");
    /* Only the Z80 setting has them; the eZ80 does not. */
    insn_is("sll b", "ERR");

    /* Suffixes select the operand size for one instruction. Which bit a one
     * or two letter suffix means depends on the current ADL mode. */
    insn_is(".assume adl=1\n    ld a,(1234h)", "3A 34 12 00");
    insn_is(".assume adl=1\n    ld.sis a,(1234h)", "40 3A 34 12");
    insn_is(".assume adl=0\n    ld a,(1234h)", "3A 34 12");
    insn_is(".assume adl=0\n    ld.lil a,(1234h)", "5B 3A 34 12 00");
    insn_is("rst.lil 10h", "5B D7");

    /* Bit numbers, restart targets and interrupt modes fold into the opcode
     * and are checked rather than truncated. */
    insn_is("bit 7,a", "CB 7F");
    insn_is("bit 8,a", "ERR");
    insn_is("rst 38h", "FF");
    insn_is("rst 39h", "ERR");
    insn_is("im 0", "ED 46");
    insn_is("im 1", "ED 56");
    insn_is("im 2", "ED 5E");
    insn_is("im 3", "ERR");

    /* A displacement out of range is refused, not wrapped. */
    insn_is("ld a,(ix+127)", "DD 7E 7F");
    insn_is("ld a,(ix+128)", "ERR");
    insn_is("ld a,(ix-128)", "DD 7E 80");
    insn_is("ld a,(ix-129)", "ERR");

    /* Operands that do not name a real form. */
    insn_is("ld a,sp", "ERR");
    insn_is("ld a,b,c", "ERR");

    /* An 8-bit immediate can be a forward reference, so an undefined one has
     * to be caught at the end rather than emitted as the zero it starts as.
     * "ld a, ab" used to assemble quietly to 3E 00. */
    insn_is("ld a,undefined_thing", "ERR");

    /* Nothing may follow the operands. "ld a, a'" used to match "ld a,a" and
     * ignore the stray literal after it. */
    insn_is("ld a,a'", "ERR");
    insn_is("ld a,b c", "ERR");

    /* A relative jump computes its displacement from the next instruction. */
    insn_is("jr $", "18 FE");
    insn_is("djnz $", "10 FE");

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
