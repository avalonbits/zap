; Refused by both. A bit number above seven does not fit the three bits it
; folds into, and masking it silently assembles a different instruction.
    .assume adl=1
    .org $40000
    bit 8, a
