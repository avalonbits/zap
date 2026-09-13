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
; And the value is sixteen bits before it is a signed byte. The reference
; holds this field in two bytes, so 0x40018 is 0x18 and in range; 0x1008 is
; 4104 and is not, which index_disp_toobig.s covers.
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
    ld   a, (ix+wide)
    ld   a, (ix+low)
    ld   a, (ix+edge)
    ld   a, (ix+medge)
field: EQU 5
wide:  EQU 0x40018
low:   EQU 0x4FFFB
edge:  EQU 127
medge: EQU -128
