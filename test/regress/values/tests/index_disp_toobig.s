; A forward index displacement that does not fit, which both refuse.
;
; 0x1008 is 4104 once truncated to the sixteen bits the reference keeps this
; field in, and a displacement is a signed byte. The check cannot happen where
; the instruction is written -- there is nothing to check yet -- so it happens
; when the fixup is settled, which is what the reference does too.
    .assume adl=1
    .org $40000
    ld   a, (ix+toobig)
toobig: EQU 0x1008
