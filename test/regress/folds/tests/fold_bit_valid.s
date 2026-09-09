; Every shape of an operand that folds into the opcode, at both ends of the
; field it folds into. zap masked these instead of checking them, so
; `bit 8, a` was `bit 0, a` and this file is the reason that cannot come back.
    .assume adl=1
    .org $40000
    bit 0, a
    bit 7, a
    bit 3, b
    bit 5, (hl)
    bit 2, (ix+1)
    bit 6, (iy-2)
    set 0, a
    set 7, l
    set 4, (hl)
    set 1, (ix+0)
    res 0, a
    res 7, h
    res 2, (hl)
    res 6, (iy+3)
