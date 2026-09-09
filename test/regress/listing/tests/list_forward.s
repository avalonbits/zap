; A forward reference is emitted as zeroes and patched when its label is
; settled, long after its line was listed. A listing written as the assembly
; goes therefore showed `ld hl, ahead` as 21 00 00 00, where the reference --
; which lists on its second pass -- shows the address.
;
; Every listed line that leaves a fixup behind is remembered, and its byte
; columns are written again from the finished output before the file is
; closed. This is what says so, in every width a fixup has: one byte, two,
; three, four, and a relative displacement.
    .assume adl=1
    .org $40000
start:
    ld hl, ahead
    call ahead
    jp far
    jr fwd
fwd:
    jr nz, ahead
    ld a, small
    dw ahead
    dl ahead
    dw32 ahead
    db 1,2,3,4,5,6, ahead, 9
    ld hl, ahead - start
small: EQU 7
ahead:
    nop
far:
    ret
