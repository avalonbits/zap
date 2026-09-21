; A FILLBYTE that comes after the reservations it does *not* decide.
;
; A reservation is a gap filled when the next byte is written, with the value
; in force at that moment, and nothing reaches backwards. So the first three
; runs below take 0xFF, the default, because no FILLBYTE has been reached when
; they are written; the fourth takes 0xAA, in force where it stands; and the
; ALIGN at the end takes 0xBB.
;
; 2.2 answered the first three differently -- 0xBB, the file's last value --
; because its `fillbyte` survived the pass boundary and the gaps were filled
; in pass two. There is no pass two in 2.3 and none here, so the rule is the
; one a single pass can state: the value in force, where the run stands.
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
