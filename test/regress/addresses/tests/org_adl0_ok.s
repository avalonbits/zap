; Out of ADL mode an address is two bytes, and this is the largest one that
; fits. zap took anything at all here.
    .assume adl=0
    .org $FFFF
    nop
