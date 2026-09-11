; A FILLBYTE that comes after the reservations it decides, which in the
; reference it may.
;
; A reservation there is a gap filled when the next byte is written, with the
; FILLBYTE in force at that moment -- and the reference's `fillbyte` survives
; the pass boundary, so pass two starts with the value the *last* FILLBYTE in
; the file left behind. So the first three runs below take 0xBB, the file's
; last value, however far above it they are; the fourth takes 0xAA, in force
; where it stands; and the ALIGN at the end takes 0xBB again. zap remembers
; the runs of the first kind and fills them at the end of the source.
    .assume adl=1
    .org $40000
    ds 2
    nop
    align 8
    nop
    ds 3
    ds 1
    nop
    fillbyte 0xAA
    ds 2
    nop
    fillbyte 0xBB
    align 16
    ret
