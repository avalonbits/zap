; The ordinary listing: an address, up to four bytes a row, the line number and
; the line as it was written. Every line here refers only backwards, because a
; forward reference is listed before it is patched here and after it is patched
; there -- one of the four things a single pass cannot do, and the reason this
; group exists rather than the whole regression tree being listed.
    .assume adl=1
    .org $40000
val: EQU 9
here:
    nop
    ld hl, here
    ld a, val
    ld bc, 0x123456
    ret
