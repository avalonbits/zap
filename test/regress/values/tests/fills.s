; What a reservation, an alignment and a block emit, which is not the same
; thing three times. DS reserves and takes the FILLBYTE; ALIGN pads to the
; next boundary; BLK emits its own fill. The listing shows the three
; differently in the reference and identically here -- bytes are what this
; file is for, and the difference is in docs/DESIGN.md.
;
; The FILLBYTE is first here so that every reservation below takes it where it
; stands. fills_late.s is the other half: what a FILLBYTE that comes after a
; reservation does to it.
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
