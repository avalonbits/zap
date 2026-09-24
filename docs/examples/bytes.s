; A small library of byte routines, written in assembly and called from C.
;
; Assemble it for agondev with `zap bytes.s bytes.o -f elf`, or for acc with
; `zap bytes.s bytes.o -f acc`. The source is the same either way; see
; docs/zap-with-agondev.md and docs/zap-with-acc.md.
;
; A name C can call starts with an underscore: C's fill() is _fill here.
; Arguments are on the stack, three bytes each, the first at (sp+3); the
; result comes back in HL (or A for a char); and every register except IX
; may be changed.

        XDEF    _fill, _sum_bytes, _apply_twice, _greeting, _apply_calls

        SEGMENT CODE

; void fill(void *dst, unsigned char value, int len)
;
; `adc hl, bc` into a zeroed HL is the 24-bit test for BC being zero: in ADL
; mode it sets Z on all three bytes, where `ld a, b / or a, c` would only see
; the low two.
_fill:
        ld      iy, 0
        add     iy, sp
        ld      bc, (iy+9)          ; len
        ld      hl, 0
        or      a, a
        adc     hl, bc
        ret     z                   ; nothing to fill
        ld      hl, (iy+3)          ; dst
        ld      a, (iy+6)           ; value
        ld      (hl), a
        dec     bc                  ; the first byte is written
        push    hl
        ld      hl, 0
        or      a, a
        adc     hl, bc
        pop     hl
        ret     z                   ; and it was the only one
        push    hl                  ; copy each byte to the next
        pop     de
        inc     de
        ldir
        ret

; int sum_bytes(const unsigned char *p, int len)
;
; Walks a pointer to the end of the bytes rather than counting down, so the
; loop's test is one 24-bit subtraction.
_sum_bytes:
        ld      iy, 0
        add     iy, sp
        ld      de, (iy+3)          ; p
        ld      hl, (iy+6)          ; len
        add     hl, de
        push    hl
        pop     iy                  ; the end
        ld      hl, 0               ; the sum
        ld      bc, 0               ; each byte goes in C, the rest stays 0
@loop:  push    hl
        lea     hl, iy+0
        or      a, a
        sbc     hl, de              ; at the end yet?
        pop     hl
        ret     z
        ld      a, (de)
        inc     de
        ld      c, a
        add     hl, bc
        jr      @loop

; int apply_twice(int (*fn)(int), int x): fn(fn(x)), calling back into C.
; Each call pushes the argument, calls, and takes the argument off again.
_apply_twice:
        ld      iy, 0
        add     iy, sp
        ld      hl, (iy+6)          ; x
        ld      de, (iy+3)          ; fn
        push    de                  ; keep fn for the second call
        push    hl
        call    call_de
        pop     de
        pop     de                  ; fn again
        push    de
        push    hl                  ; the first result is the next argument
        call    call_de
        pop     de
        pop     de
        ld      de, (_apply_calls)
        inc     de
        ld      (_apply_calls), de
        ret

; Calls the function DE points at. A name with no underscore is the
; library's own, and C can't see it.
call_de:
        push    de
        ret

        SEGMENT RODATA
_greeting:
        db      "Hello from zap", 0

        SEGMENT BSS
_apply_calls:
        ds      3
