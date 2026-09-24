; Assembly functions called from C, assembled by zap as an object and linked
; by agondev. Each one reads its arguments from the slots agondev's calling
; convention puts them in and returns its result where agondev expects it, so
; a mistake in either is a wrong answer in test/abi/src/main.c.
;
; The convention, as acc's test/abi.sh pins it: arguments are pushed right to
; left, each in a 3-byte slot (6 for long, 9 for long long), the first at
; (sp+3) on entry; the caller removes them; a result is A for 1 byte, HL for 2
; or 3, E:HL for 4, and HL, DE, BC for 8; a struct comes back through a hidden
; first argument, returned in HL. Only IX has to survive a call.

        XDEF    _asm_add3, _asm_char_sum, _asm_short_sub, _asm_long_add
        XDEF    _asm_llong_pass, _asm_after_llong, _asm_store, _asm_make_pair
        XDEF    _asm_call_c, _asm_bump, _asm_clobber
        XDEF    _asm_table_hi, _asm_table_up, _asm_table_lo
        XDEF    _asm_table, _asm_words, _asm_message, _asm_buffer, _asm_table_end

        XREF    _c_sub, _c_counter

        SEGMENT CODE

; int asm_add3(int a, int b, int c)
_asm_add3:
        ld      iy, 0
        add     iy, sp
        ld      hl, (iy+3)
        ld      de, (iy+6)
        add     hl, de
        ld      de, (iy+9)
        add     hl, de
        ret

; char asm_char_sum(char a, char b)
_asm_char_sum:
        ld      iy, 0
        add     iy, sp
        ld      a, (iy+3)
        add     a, (iy+6)
        ret

; short asm_short_sub(short a, short b)
_asm_short_sub:
        ld      iy, 0
        add     iy, sp
        ld      hl, (iy+3)
        ld      de, (iy+6)
        or      a, a
        sbc     hl, de
        ret

; long asm_long_add(long a, long b): each in a 6-byte slot, the result in E:HL
_asm_long_add:
        ld      iy, 0
        add     iy, sp
        ld      hl, (iy+3)
        ld      de, (iy+9)
        add     hl, de
        ld      a, (iy+6)
        adc     a, (iy+12)
        ld      e, a
        ret

; long long asm_llong_pass(long long a): returned whole, in HL, DE and BC
_asm_llong_pass:
        ld      iy, 0
        add     iy, sp
        ld      hl, (iy+3)
        ld      de, (iy+6)
        ld      bc, (iy+9)
        ret

; int asm_after_llong(long long a, int b): b is past a 9-byte slot
_asm_after_llong:
        ld      iy, 0
        add     iy, sp
        ld      hl, (iy+12)
        ret

; void asm_store(int *p, int v)
_asm_store:
        ld      iy, 0
        add     iy, sp
        ld      hl, (iy+6)
        ld      iy, (iy+3)
        ld      (iy+0), hl
        ret

; Pair asm_make_pair(int x): the Pair's address is the hidden first argument,
; and comes back in HL
_asm_make_pair:
        ld      iy, 0
        add     iy, sp
        ld      hl, (iy+6)
        ld      iy, (iy+3)
        ld      (iy+0), hl
        inc     hl
        ld      (iy+3), hl
        lea     hl, iy+0
        ret

; int asm_call_c(int a, int b): c_sub(a, b) + 1, calling back into C. The
; arguments go on right to left, and the caller takes them off again.
_asm_call_c:
        ld      iy, 0
        add     iy, sp
        ld      hl, (iy+6)
        push    hl
        ld      hl, (iy+3)
        push    hl
        call    _c_sub
        pop     de
        pop     de
        inc     hl
        ret

; void asm_bump(void): a C global, through an import
_asm_bump:
        ld      hl, (_c_counter)
        inc     hl
        ld      (_c_counter), hl
        ret

; void asm_clobber(void): every register but IX and SP, including the
; alternate set, which C must not have been relying on
_asm_clobber:
        ld      hl, 0x5A5A5A
        ld      de, 0x5A5A5A
        ld      bc, 0x5A5A5A
        ld      iy, 0x5A5A5A
        ld      a, 0x5A
        exx
        ex      af, af'
        ld      hl, 0xA5A5A5
        ld      de, 0xA5A5A5
        ld      bc, 0xA5A5A5
        ld      a, 0xA5
        ex      af, af'
        exx
        ret

; Each byte of an address, and its low 16 bits, as relocations
_asm_table_lo:
        ld      a, _asm_table & 0xFF
        ret

_asm_table_hi:
        ld      a, _asm_table >> 8
        ret

_asm_table_up:
        ld      a, _asm_table >> 16
        ret

        SEGMENT DATA
_asm_table:
        dl      1, 2, 0x123456
_asm_table_end:
        dl      _asm_table_end - _asm_table
_asm_words:
        dw      _asm_table, _asm_table_end - _asm_table

        SEGMENT RODATA
_asm_message:
        db      "zap", 0

        SEGMENT BSS
_asm_buffer:
        ds      16
