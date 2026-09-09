; A reservation takes the FILLBYTE and drops whatever is written after the
; count -- these four bytes are FF and not AA. The reference says so and zap
; said nothing; the bytes were always the same, which is why only the message
; needed fixing and why this file pins the bytes.
    .assume adl=1
    .org $40000
    ds 4, 0xAA
    nop
    ds 2, 1, 2
    nop
