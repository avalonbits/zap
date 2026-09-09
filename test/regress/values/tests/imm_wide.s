; An instruction's immediate is not checked against 24 bits by the reference
; -- it takes the low three bytes and says nothing -- while a directive is.
; zap warned about these, and only on a host where an int is four bytes.
    .assume adl=1
    .org $40000
    ld hl, 0x12345678
    ld bc, 0x1234567
    ld a, 0x12345678
