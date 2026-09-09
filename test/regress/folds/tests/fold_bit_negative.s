; A bit number below zero is not refused by the reference: it shifts the whole
; value into the opcode without masking, so `bit -1, a` is CB FF and not CB 7F.
; zap indexes a table of shifts for 0..7 and has to shift for these.
    .assume adl=1
    .org $40000
    bit -1, a
    bit -1, (hl)
    set -3, (ix+4)
    res -8, b
