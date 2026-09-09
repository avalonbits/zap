; A restart address that is still ahead of the instruction using it. Not `r`
; for the label: that is the refresh register, and `rst r` is refused by both
; as an operand that does not match -- which agrees, and would have tested
; nothing.
    .assume adl=1
    .org $40000
    rst vec
    rst top
vec: EQU 8
top: EQU 0x38
