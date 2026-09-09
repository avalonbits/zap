; The awkward one. An index form puts the displacement *between* the CB prefix
; and the opcode -- DD CB 02 B6 -- so a fold that has to wait for its label
; cannot leave the fixup on the byte after the prefixes. It is the only shape
; where the opcode is not where counting prefixes would put it.
;
; Not `m` for the label: that is the sign-negative condition code, and
; `res m, (ix+2)` is refused by both as an operand that does not match, which
; agrees and tests nothing.
    .assume adl=1
    .org $40000
    res k, (ix+2)
    bit j, (iy-1)
k:  EQU 6
j:  EQU 2
