; Out of ADL mode an address is two bytes, so a wider one is truncated to
; sixteen bits and warned about in both.
    .assume adl=0
    .org $100
    ld hl, 0x123456
    ld a, 300
