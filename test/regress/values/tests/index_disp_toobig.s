; A forward index displacement that does not fit, which both refuse.
;
; A displacement is a signed byte and 0x1008 is 4104, so it is out of range
; whatever width the value was held in -- 2.2 refused it after truncating to
; sixteen bits and 2.3 refuses it without. The check cannot happen where the
; instruction is written, because there is nothing to check yet, so it happens
; when the fixup is settled, which is what the reference does too.
    .assume adl=1
    .org $40000
    ld   a, (ix+toobig)
toobig: EQU 0x1008
