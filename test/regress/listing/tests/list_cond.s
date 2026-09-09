; Conditional assembly: the lines that are skipped are listed too, with no
; bytes against them.
    .assume adl=1
    .org $40000
flag: EQU 1
    IF flag
    nop
    ELSE
    ret
    ENDIF
    IF 0
    db 1,2,3
    ENDIF
    nop
