; BLK emits rather than reserves, so its bytes are listed like any others.
    .assume adl=1
    .org $40000
    blkb 5, 0xAA
    blkw 2, 0x1234
    blkp 1, 0x123456
    nop
