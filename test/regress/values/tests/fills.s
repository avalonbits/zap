; What a reservation, an alignment and a block emit, which is not the same
; thing three times. DS reserves and takes the FILLBYTE; ALIGN pads to the
; next boundary; BLK emits its own fill. The listing shows the three
; differently in the reference and identically here -- bytes are what this
; file is for, and the difference is in docs/DESIGN.md.
;
; The FILLBYTE is first because a later one is the one refusal zap keeps in
; this area: the reference fills every reservation at write-out, so a FILLBYTE
; after a DS changes bytes already emitted here. Also in completeness.md.
    .assume adl=1
    .org $40000
    fillbyte 0x55
    nop
    ds 3
    nop
    align 4
    nop
    blkb 5, 0xAA
    blkw 2, 0x1234
    blkp 1, 0x123456
    ds 2
    nop
    align 8
    ret
