/*
 * Copyright (C) 2023  Igor Cananea <icc@avalonbits.com>
 * Author: Igor Cananea <icc@avalonbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INSN_H
#define INSN_H

#include "zap.h"

static inline uint8_t* emit_imm(uint8_t* o, const dop* op, uint8_t cond, bool adl) {
    const int width = (cond & IMM_N) ? 1 : (adl ? 3 : 2);

    /* Written out rather than looped, and reading op->imm afresh each time
     * rather than through a local. The loop's `>> (i * 8)` is a variable shift
     * and cost a call to __ishru per byte. A constant shift cast to uint8_t is
     * better but not free: from a local the compiler still calls __ishru for
     * `>> 16`, because the value is in a stack slot it has already loaded as a
     * whole. Left as a field read it is an indexed load of the one byte
     * wanted -- `ld a, (iy+n)` -- for all three. */
    *o++ = (uint8_t) op->imm;
    if (width > 1) {
        *o++ = (uint8_t) (op->imm >> 8);
    }
    if (width > 2) {
        *o++ = (uint8_t) (op->imm >> 16);
    }

    return o;
}

/* Inlined into callers in other files, so the bodies live here. */

/* The same bucket, reached the way the hot path wants it.
 *
 * bucket_of clamps the length, and that clamp is a *signed* compare: eleven
 * instructions and a `call pe, __setflag` to fix the flags up on overflow, on
 * every line of the source. A token of eight characters or more is not a
 * mnemonic -- the longest is five -- so the clamp can be a rejection instead,
 * and unsigned it is one compare.
 *
 * build_tables keeps using bucket_of, where the cost does not matter and the
 * clamp rather than the rejection is what the table wants. */
static inline const insninfo* bucket_at(char first, unsigned n) {
    if (n >= NLEN) {
        return NULL;
    }

    return bucket_head[letter_base[(uint8_t) first] + n].head;
}

/* Two things this does not do, because the bucket has already done them.
 *
 * It starts at index 1: the bucket is chosen by letter_base[first], which maps
 * 'a' and 'A' to the same base, so every candidate in the chain already agrees
 * with the source on its first character.
 *
 * And it case-folds one side, not two. Every name in the table is lower case,
 * which is checked when the table is built, so only the source needs the OR. */
static inline bool same_ci(const char* name, const char* s, int n) {
    /* Unsigned, and that is not incidental: two signed ints compared with `<`
     * cannot be done in one subtract, so the compiler emits a
     * `call pe, __setflag` to repair the flags on overflow -- here, inside the
     * loop that compares a mnemonic on every line of the source. */
    for (unsigned i = 1; i < (unsigned) n; i++) {
        if (name[i] != (s[i] | 0x20)) {
            return false;
        }
    }

    return true;
}

static inline const insninfo* mnemonic_of(const char* s, int n) {
#ifdef MTRUNC
    /* The bucket and not the walk, so the two halves of the lookup can be told
     * apart. Built with -DTRUNC=4 -DTRUNC_NODIR, where nothing reads the
     * answer. */
    trunc_sink = bucket_at(s[0], (unsigned) n) != NULL;

    return NULL;
#endif
    for (const insninfo* ins = bucket_at(s[0], (unsigned) n); ins != NULL;
         ins = ins->next) {
        /* No length test. The bucket is keyed by first character *and*
         * length, and the clamp at NLEN is never reached because the longest
         * mnemonic is five characters -- checked when the table is built --
         * so every candidate in this chain already has the length wanted. A
         * token longer than the clamp lands in a bucket that holds nothing. */
        if (same_ci(ins->name, s, n)) {
            return ins;
        }
    }

    return NULL;
}

/* The four ungrouped mnemonics: call, jp, jr and ret.
 *
 * Ten rows between them, each carrying its own mode test, because a row that
 * takes a condition code has to be reached whatever mode the operands were
 * parsed as. Kept out of line so that the register test appears once in the
 * hot path rather than twice. */
__attribute__((always_inline)) static inline const isa_row* match_row_cc(
    const insninfo* insn, const dop* a, const dop* b, uint8_t want) {
    const uint8_t has_cc = (uint8_t) (a->cc != 0);
    const uint8_t a0 = a->r0, a1 = a->r1, a2 = a->r2;
    const uint8_t b0 = b->r0, b1 = b->r1, b2 = b->r2;
    const uint8_t anone = a->noreg;
    const uint8_t bnone = b->noreg;
    const rowinfo* ri = insn->rows;

    for (uint8_t n = insn->count; n != 0; n--, ri++) {
        const uint8_t ccok = ri->ccok;
        if (ri->modes != want && !(ccok & has_cc)) {
            continue;
        }
        if ((uint8_t) ((ri->a0 & a0) | (ri->a1 & a1) | (ri->a2 & a2)
                       | (ri->aempty & anone) | ccok) != 0
            && (uint8_t) ((ri->b0 & b0) | (ri->b1 & b1) | (ri->b2 & b2)
                          | (ri->bempty & bnone)) != 0) {
            const isa_row* row = ri->row;
            if ((row->cpu & cpu_mask) == 0) {
                return NULL;
            }

            return row;
        }
    }

    return NULL;
}

/* always_inline, and it has to be: this has two callers, the ordinary path and
 * the suffixed one, and a second caller is enough for the compiler to stop
 * inlining it into the first -- which is the path every instruction takes. */
__attribute__((always_inline)) static inline const isa_row* match_row(const insninfo* insn,
                                                         const dop* a,
                                                         const dop* b) {
    const uint8_t want = (uint8_t) (shl4[a->mode & 15] | (b->mode & 15));

    /* Find the group, then scan it. Two loops rather than one, because the
     * two questions have nothing in common: which shape of operands the row
     * wants, and which registers.
     *
     * Asked as one loop, rejecting a group cost a whole turn of it -- the
     * counter test, moving the row pointer into iy, loading the mode and the
     * ccok flag, then the skip count and the next pointer, twenty instructions
     * to step over rows that could not match. The group table is five bytes a
     * row and rejecting one is a compare and a step. */
    uint8_t n = insn->ngroups;
    if (n == 0) {
        return match_row_cc(insn, a, b, want);
    }

    const grpinfo* g = insn->groups;
    while (g->modes != want) {
        if (--n == 0) {
            return NULL;
        }
        g++;
    }

    /* Already split, by whoever recognised the register. */
    const uint8_t a0 = a->r0, a1 = a->r1, a2 = a->r2;
    const uint8_t b0 = b->r0, b1 = b->r1, b2 = b->r2;
    const uint8_t anone = a->noreg;
    const uint8_t bnone = b->noreg;

    /* Operand A on its own, and B only if A survives.
     *
     * The question is whether the operand shares a bit with what the row
     * accepts, so the bits are tested where they are rather than turned into
     * two 0/1 values and ANDed -- each `(g != 0)` would be a compare and a
     * branch to pick between two constants, and both sides would be computed
     * before either was looked at.
     *
     * This is the line to spend care on: several rows reach the register test
     * for every instruction, and most rejections are rows of the right shape
     * with the wrong registers. Rejecting on A alone skips B entirely for the
     * majority of them.
     *
     * No mode test here and no ccok test: being in the group is the answer,
     * and a mnemonic with a ccok row anywhere in it has no groups at all. */
    const rowinfo* ri = g->rows;
    for (uint8_t k = g->count; k != 0; k--, ri++) {
        if ((uint8_t) ((ri->a0 & a0) | (ri->a1 & a1) | (ri->a2 & a2)
                       | (ri->aempty & anone)) != 0
            && (uint8_t) ((ri->b0 & b0) | (ri->b1 & b1) | (ri->b2 & b2)
                          | (ri->bempty & bnone)) != 0) {
            const isa_row* row = ri->row;
            if ((row->cpu & cpu_mask) == 0) {
                return NULL;
            }

            return row;
        }
    }

    return NULL;
}

__attribute__((always_inline)) static inline uint8_t ddfd_prefix(const dop* op) {
    if ((op->r1 & RP1_IX) != 0) {
        return 0xDD;
    }
    if (((op->r1 & RP1_IY) | (op->r2 & RP2_IY)) != 0) {
        return 0xFD;
    }

    return 0;
}

__attribute__((always_inline)) static inline uint8_t transform(emitted* out, dop* op, uint8_t type) {
    switch (type) {
        case TR_IR0:
            if (((op->r1 & RP1_XYL) | (op->r2 & RP2_XYL)) != 0) {
                out->opcode |= 0x01;
            }
            break;
        case TR_IR3:
            if (((op->r1 & RP1_XYL) | (op->r2 & RP2_XYL)) != 0) {
                out->opcode |= 0x08;
            }
            break;
        case TR_Z:
            out->opcode |= op->reg_index;
            break;
        case TR_Y:
            if ((op->mode & IMM) != 0) {
                /* The bit number of `bit n, (hl)` and `bit n, (ix+d)`. The
                 * register forms use TR_BIT and are checked there; every other
                 * TR_Y is a register in this slot and never takes this arm. */
                if (op->fwd != NULL) {
                    op->mode &= (uint8_t) ~IMM;

                    return TRF_DEFER;
                }
                if (op->imm > 7) {
                    state.err = ZAP_E_INVALID_BIT_NUMBER;

                    return TRF_ERR;
                }
                /* Shifted rather than looked up in a table, and not masked,
                 * because the reference does not mask: `bit -1, a` is CB FF
                 * there -- the whole shifted value ORed into the opcode. A
                 * table indexed by `v & 7` would give CB 7F. */
                out->opcode |= (uint8_t) ((unsigned) op->imm << 3);
            } else {
                out->opcode |= shl3[op->reg_index & 7];
            }
            break;
        case TR_P:
            out->opcode |= shl4[op->reg_index & 15];
            break;
        case TR_CC:
            out->opcode |= shl3[op->cc_index & 7];
            break;
        case TR_N:
            /* RST, and the only row with this transform. The eight addresses
             * are the multiples of eight below 0x40, which is every value that
             * ORs into 0xC7 without touching a bit that is already there. */
            op->mode &= (uint8_t) ~IMM;
            if (op->fwd != NULL) {
                return TRF_DEFER;
            }
            if (((unsigned) op->imm & ~0x38u) != 0) {
                state.err = ZAP_E_RESTART_ADDRESS;

                return TRF_ERR;
            }
            out->opcode |= (uint8_t) op->imm;
            break;
        case TR_BIT:
            op->mode &= (uint8_t) ~IMM;
            if (op->fwd != NULL) {
                return TRF_DEFER;
            }
            if (op->imm > 7) {
                state.err = ZAP_E_INVALID_BIT_NUMBER;

                return TRF_ERR;
            }
            out->opcode |= (uint8_t) ((unsigned) op->imm << 3);
            break;
        case TR_SELECT: {
            /* IM, and the only row with this transform. Modes 0, 1 and 2 are
             * y = 0, 2 and 3. Above 2 the reference refuses; below 0 it does
             * not, and assembles `im 0`, so neither does this. */
            op->mode &= (uint8_t) ~IMM;
            if (op->fwd != NULL) {
                return TRF_DEFER;
            }
            uint8_t y = 0;
            if (op->imm == 1) {
                y = 2;
            } else if (op->imm == 2) {
                y = 3;
            } else if (op->imm > 2) {
                state.err = ZAP_E_INTERRUPT_MODE;

                return TRF_ERR;
            }
            out->opcode |= shl3[y & 7];
            break;
        }
        default:
            break;
    }

    return TRF_OK;
}

__attribute__((always_inline)) static inline bool emit_row(const isa_row* row, dop* a, dop* b, uint8_t suffix) {
    if (!out_reserve()) {
        return false;
    }

    /* One cursor for the whole instruction rather than state.out[state.pos++] per
     * byte. put() reloaded both the output base and the position, added them,
     * stored the byte and stored the position back, for every byte written --
     * twenty-three loads of those two fields in this function alone. The
     * reservation above is what makes a bare cursor safe: room for the
     * longest form is already there, so nothing between here and the
     * write-back can move the buffer. */
    ETRUNC_AT(1);

    uint8_t* o = state.o;

    /* The suffix byte goes in front of everything, including the DD or FD an
     * index register brings: `ld.lil ix, nn` is 5B DD 21 ...
     *
     * Whether the row takes this suffix is asked here rather than while the
     * row is chosen, because the row is chosen by the shape of the operands
     * and this is not one of them. `ld.lil a, b` matches the register-to-
     * register row perfectly well and is still refused -- "Suffix not matching
     * mnemonic / ADL mode" in the reference -- since a suffix only means
     * something where the instruction touches memory, the stack or an
     * address. Which rows those are is in the table already; the generator
     * transcribed the field with the rest of the row. */
    /* Nothing here survives into the ordinary path: emit_row is always
     * inlined and its one hot caller passes a literal zero, so the whole of
     * this folds away there. The suffixed copy lives in uncommon_line. */
    if (suffix != 0) {
        if ((row->flags & suffix) == 0) {
            state.err = ZAP_E_INSTRUCTION_NO_MODE_SUFFIX;

            return false;
        }
        *o++ = suffix_code(suffix);
    }

    emitted out;
    out.prefix1 = 0;
    out.prefix2 = row->prefix;
    out.opcode = row->opcode;

    if (row->flags & F_DDFDOK) {
        const uint8_t p1 = ddfd_prefix(a);
        const uint8_t p2 = ddfd_prefix(b);
        out.prefix1 = ((p1 == 0 && p2 != 0) || ((a->mode & INDIRECT) == 0 && p1 != 0 && p2 != 0))
                      ? p2 : p1;
    }

    /* Tested rather than called. transform is a real function with a switch
     * in it, and TR_NONE is the commonest case by a wide margin -- every
     * instruction whose operands do not fold into the opcode. A load and a
     * compare replaces a call, a dispatch and a return. */
    if (row->transformA != TR_NONE) {
        const uint8_t r = transform(&out, a, row->transformA);
        if (r != TRF_OK
            && (r == TRF_ERR
                || !fold_defer(row->transformA, a, out.prefix1, out.prefix2,
                               row->flags, (int) (o - state.out)))) {
            return false;
        }
    }
    if (row->transformB != TR_NONE) {
        const uint8_t r = transform(&out, b, row->transformB);
        if (r != TRF_OK
            && (r == TRF_ERR
                || !fold_defer(row->transformB, b, out.prefix1, out.prefix2,
                               row->flags, (int) (o - state.out)))) {
            return false;
        }
    }

    ETRUNC_AT(2);

    /* The ordinary shape first: no index prefix and no displacement, which is
     * every instruction that is not an `(ix+d)` form.
     *
     * The chain below is six tests to place at most four bytes, and one of
     * them -- whether the opcode goes after the displacement rather than
     * before it -- is only ever true for `bit n, (ix+d)` and its relatives:
     * DD or FD, then CB, then a displacement. Computing that on every
     * instruction, and testing it twice, is what this skips. `prefix1` is zero
     * whenever there is no index register, so one load answers it. */
    const uint8_t dflags = (uint8_t) (row->flags & (F_DISPA | F_DISPB));
    if (out.prefix1 == 0 && dflags == 0) {
        if (out.prefix2 != 0) {
            *o++ = out.prefix2;
        }
        *o++ = out.opcode;
    } else {
        const bool dd_before_opcode =
            (out.prefix1 == 0xDD || out.prefix1 == 0xFD) && out.prefix2 == 0xCB
            && dflags != 0;

        if (out.prefix1 != 0) {
            *o++ = out.prefix1;
        }
        if (out.prefix2 != 0) {
            *o++ = out.prefix2;
        }
        if (!dd_before_opcode) {
            *o++ = out.opcode;
        }
        /* A displacement whose value is still ahead leaves a placeholder and
         * a fixup on the byte it occupies. Its position is whatever it is --
         * `bit n, (ix+d)` puts it *before* the opcode -- and this is the one
         * place that knows, which is why the fixup is recorded here. */
        if (dflags & F_DISPA) {
            if ((a->mode & DISPFWD) != 0
                && !fix_add(a->fwd, a->fwd2, a->disp,
                            (uint8_t) (((a->mode & DISPNEG) ? FIX_DISP_NEG
                                                            : FIX_DISP)
                                       | (a->fwd2_neg ? FIX_SUB2 : 0)),
                            (int) (o - state.out))) {
                return false;
            }
            *o++ = (uint8_t) (a->disp & 0xFF);
        }
        if (dflags & F_DISPB) {
            if ((b->mode & DISPFWD) != 0
                && !fix_add(b->fwd, b->fwd2, b->disp,
                            (uint8_t) (((b->mode & DISPNEG) ? FIX_DISP_NEG
                                                            : FIX_DISP)
                                       | (b->fwd2_neg ? FIX_SUB2 : 0)),
                            (int) (o - state.out))) {
                return false;
            }
            *o++ = (uint8_t) (b->disp & 0xFF);
        }
        if (dd_before_opcode) {
            *o++ = out.opcode;
        }
    }

    ETRUNC_AT(3);

    /* A relative displacement is measured from the instruction after this one,
     * so it is the last thing written and needs no width decision. */
    if (row->transformA == TR_REL || row->transformB == TR_REL) {
        const dop* rel = (row->transformA == TR_REL) ? a : b;
        if (rel->fwd != NULL) {
            /* Forward: the displacement is not known, so a zero goes down and
             * the fixup carries the address it will be measured from. Whether
             * it is in reach is decided when it is patched. */
            if (!fix_add(rel->fwd, rel->fwd2, rel->imm,
                         rel->fwd2_neg ? FIX_SUB2 : 0,
                         (int) (o - state.out))) {
                return false;
            }
            *o++ = 0;
        } else {
            const int d = rel->imm - (state.org + (int) (o - state.out) + 1);
            if (d < -128 || d > 127) {
                state.err = ZAP_E_RELATIVE_JUMP_TOO_FAR;

                return false;
            }
            *o++ = (uint8_t) d;
        }
    } else {
        /* How wide an address is: the suffix says, if there is one, and the
         * ADL mode says otherwise. `ld.lis hl, 0x1234` is two bytes of
         * immediate in ADL mode and `ld.sil hl, 0x123456` is three out of it,
         * so this cannot read the mode alone.
         *
         * Asked inside each branch rather than once above them. Hoisted, it is
         * a live value across both and an instruction with no immediate --
         * which is most of them -- computes it for nothing. */
#define SFX_WIDE (suffix != 0 ? (suffix & (S_SIS | S_LIS)) == 0 : state.adl)
        if ((a->mode & IMM) != 0 && (row->condA & (IMM_N | IMM_MMN))) {
            if (a->fwd != NULL
                && !fix_add(a->fwd, a->fwd2, a->imm,
                            (uint8_t) (((row->condA & IMM_N) ? 1
                                                             : (SFX_WIDE ? 3 : 2))
                                       | (a->fwd2_neg ? FIX_SUB2 : 0)),
                            (int) (o - state.out))) {
                return false;
            }
            warn_imm(a->imm, (row->condA & IMM_N) ? 1 : (SFX_WIDE ? 3 : 2));
            o = emit_imm(o, a, row->condA, SFX_WIDE);
        }
        if ((b->mode & IMM) != 0 && (row->condB & (IMM_N | IMM_MMN))) {
            if (b->fwd != NULL
                && !fix_add(b->fwd, b->fwd2, b->imm,
                            (uint8_t) (((row->condB & IMM_N) ? 1
                                                             : (SFX_WIDE ? 3 : 2))
                                       | (b->fwd2_neg ? FIX_SUB2 : 0)),
                            (int) (o - state.out))) {
                return false;
            }
            warn_imm(b->imm, (row->condB & IMM_N) ? 1 : (SFX_WIDE ? 3 : 2));
            o = emit_imm(o, b, row->condB, SFX_WIDE);
        }
#undef SFX_WIDE
    }

    state.o = o;

    return true;
}

#endif /* INSN_H */
