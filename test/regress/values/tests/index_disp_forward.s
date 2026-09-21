; An index displacement whose value is not defined until later.
;
; `ld a, (ix+field)` with `field` an EQU further down is how a structure is
; read, and it is what a user reported zap refusing. One pass writes the
; instruction before the value is known, so the displacement byte carries a
; fixup like any other operand -- with two things that are its own.
;
; The sign outside the brackets negates the whole expression and not its first
; term: `(ix-v+1)` with v five is -6 here, as in the reference, not -4.
;
; And the value is the machine word before it is a signed byte: 2.3 keeps the
; field in an int24_t and refuses anything outside a byte, where 2.2 kept it in
; two bytes and made 0x40018 offset 0x18. index_disp_toobig.s is the refusal.
;
; The values above a byte are not here for that reason: they are in the other
; file now.
    .assume adl=1
    .org $40000
    ld   a, (ix+field)
    ld   a, (ix-field)
    ld   a, (ix+field+1)
    ld   a, (ix-field+1)
    ld   (ix+field), a
    ld   (ix+field), 0x11
    ld   a, (iy+field)
    bit  3, (ix+field)
    ld   a, (ix+edge)
    ld   a, (ix+medge)
field: EQU 5
edge:  EQU 127
medge: EQU -128
