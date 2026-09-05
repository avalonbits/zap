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

/* Like insn_is, but for a source of several lines and with its own label, so a
 * forward reference has somewhere to be defined. */
static void insn_is_named(const char* name, const char* src, const char* want) {
    const char* got = emit(src);
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-46s %s\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-46s got %s, want %s\n", name, got, want);
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

    /* A value that folds into the opcode byte and is never defined is still an
     * error. These used to emit bit 0, rst 0 and im 0 and report success. */
    insn_is("bit later,a", "ERR");
    insn_is("rst later", "ERR");
    insn_is("im later", "ERR");

    /* But one defined further down is not, and this is what the prescan used
     * to exist for. There is no hole to leave for a value that folds into the
     * opcode, but there does not need to be one: the byte is written with the
     * operand contributing nothing, which is exactly the base the transform
     * ORs into, so settling it later sets the same bits.
     *
     * Each of these must land on a different bit field of the opcode, or a
     * fold that wrote the wrong bits would still look right. */
    {
        static const struct { const char* name; const char* src; const char* want; } folds[] = {
            {"rst with a later constant",  "    rst target\ntarget: equ 8\n",       "CF"},
            {"rst 0 with a later constant","    rst target\ntarget: equ 0\n",       "C7"},
            {"bit with a later constant",  "    bit target,a\ntarget: equ 7\n",     "CB 7F"},
            {"im with a later constant",   "    im target\ntarget: equ 2\n",        "ED 5E"},
            {"set with a later constant",  "    set target,b\ntarget: equ 3\n",     "CB D8"},
            {"res with a later constant",  "    res target,c\ntarget: equ 5\n",     "CB A9"},
            /* Through an expression, not just a bare name. */
            {"a folded expression",        "    rst target*8\ntarget: equ 1\n",     "CF"},
        };
        for (unsigned i = 0; i < sizeof(folds) / sizeof(folds[0]); i++) {
            const char* got = emit(folds[i].src);
            if (strcmp(got, folds[i].want) == 0) {
                fprintf(stderr, "PASS  %-32s %s\n", folds[i].name, got);
            } else {
                fprintf(stderr, "FAIL  %-32s got %s, want %s\n",
                        folds[i].name, got, folds[i].want);
                failures++;
            }
        }
    }

    /* The range checks travel with the deferred value -- they cannot run where
     * the instruction is written, because there is nothing to check yet. A
     * later constant that is out of range has to be caught when it arrives,
     * not silently truncated into the opcode. */
    insn_is_named("later rst address that is not a multiple of 8",
                  "    rst target\ntarget: equ 3\n", "ERR");
    insn_is_named("later bit number above 7",
                  "    bit target,a\ntarget: equ 9\n", "ERR");
    insn_is_named("later interrupt mode above 2",
                  "    im target\ntarget: equ 5\n", "ERR");

    /* A relative jump computes its displacement from the next instruction. */
    insn_is("jr $", "18 FE");
    insn_is("djnz $", "10 FE");

    /* A lone literal operand takes a fast path that skips the expression
     * evaluator, so the two ways of reaching a value have to agree.
     *
     * The literal is consumed before it is known whether an operator follows,
     * so the case where one does resumes the evaluator with the value already
     * in hand rather than starting over. If the resumed span did not begin at
     * the literal, a deferred expression would be re-evaluated from the wrong
     * text -- and silently, since it still parses. */
    insn_is("ld a,5", "3E 05");
    insn_is("ld a,1+2", "3E 03");
    insn_is("ld a,2*3+1", "3E 07");
    insn_is("ld a,1+2*3", "3E 09");
    /* ADL=1, so hl takes a 24-bit immediate. The fast path must not change
     * the width the mode decides -- it supplies a value, not a size. */
    insn_is("ld hl,0x1234", "21 34 12 00");
    insn_is("ld hl,0x1000+0x234", "21 34 12 00");
    insn_is("ld a,-1", "3E FF");
    insn_is("ld a,~0", "3E FF");
    insn_is("ld a,[1+2]*3", "3E 09");

    /* And the same literal in ADL=0, where hl is 16-bit. A fast path that
     * supplied the value without going through the mode would emit the same
     * bytes in both modes, which is the failure this catches. */
    {
        struct { const char* name; const char* src; const char* want; } modes[] = {
            {"lone literal, adl=0", "  .assume adl=0\n  ld hl,0x1234\n", "21 34 12"},
            {"sum, adl=0",          "  .assume adl=0\n  ld hl,0x1000+0x234\n", "21 34 12"},
            {"lone literal, adl=1", "  .assume adl=1\n  ld hl,0x1234\n", "21 34 12 00"},
        };
        for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
            const char* got = emit(modes[i].src);
            if (strcmp(got, modes[i].want) == 0) {
                fprintf(stderr, "PASS  %-32s %s\n", modes[i].name, got);
            } else {
                fprintf(stderr, "FAIL  %-32s got %s, want %s\n",
                        modes[i].name, got, modes[i].want);
                failures++;
            }
        }
    }

    /* The same expressions with a forward reference, so the fast path hands
     * over to the evaluator and the span has to cover the whole thing. Each is
     * compared against the identical source with the label defined first. */
    {
        static const char* exprs[] = {
            "LATER", "1+LATER", "LATER+1", "1+LATER*2", "1  +   LATER",
            "2*LATER", "LATER-1", "[1+LATER]*2", "0x10+LATER", 0
        };
        int bad = 0;
        for (int i = 0; exprs[i]; i++) {
            char src[256];
            static char before[256];
            snprintf(src, sizeof(src), "LATER: equ 5\n    ld a,%s\n", exprs[i]);
            snprintf(before, sizeof(before), "%s", emit(src));
            snprintf(src, sizeof(src), "    ld a,%s\nLATER: equ 5\n", exprs[i]);
            const char* after = emit(src);
            if (strcmp(before, after) != 0 || strcmp(after, "ERR") == 0) {
                fprintf(stderr, "FAIL  ld a,%-24s deferred %s, immediate %s\n",
                        exprs[i], after, before);
                bad++;
            }
        }
        if (bad == 0) {
            fprintf(stderr, "PASS  %-32s 9 expressions\n",
                    "deferred operand matches immediate");
        } else {
            failures += bad;
        }
    }

    if (failures) {
        fprintf(stderr, "\n%d failure(s)\n", failures);
    }

    return failures ? 1 : 0;
}
