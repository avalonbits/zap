; An ORG that skips forward pads the gap, and the pad is listed against the
; ORG line itself -- bytes on the first row, continuation underneath.
    .assume adl=1
    .org $40000
    nop
    .org $40008
    nop
    .org $40010
    ret
