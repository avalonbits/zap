; And with a FILLBYTE set, which is what the initializer is being ignored in
; favour of.
    .assume adl=1
    .org $40000
    fillbyte 0xAA
    ds 4, 0x55
    nop
