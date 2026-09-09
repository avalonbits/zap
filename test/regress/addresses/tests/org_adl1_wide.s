; Taken by both. The reference does not check ORG against the 24-bit ceiling
; in ADL mode, and a check zap added on its own would refuse a file the
; reference assembles.
    .assume adl=1
    .org $123456
    nop
