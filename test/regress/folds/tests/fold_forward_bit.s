; The fold's value is still ahead of the instruction that uses it. This is the
; one that produced wrong bytes on legal code: the operand never became an
; immediate and no fixup was left on the opcode, so `bit n, a` assembled as
; `bit 0, a` and said nothing.
    .assume adl=1
    .org $40000
    bit n, a
    bit n, (hl)
    set n, b
    res k, c
n:  EQU 3
k:  EQU 6
