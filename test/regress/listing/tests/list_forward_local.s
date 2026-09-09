; The same, for a local reference, which is settled at the end of its scope
; rather than at the end of the file -- so its line is rewritten long before
; the listing is finished.
    .assume adl=1
    .org $40000
one:
    jr @next
    ld hl, @far
@next:
    nop
@far:
    ret
two:
    jr @next
@next:
    nop
