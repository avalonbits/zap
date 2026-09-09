; Taken by both: RELOCATE is not checked against sixteen bits out of ADL mode,
; even though ORG is.
    .assume adl=0
    .org $100
    .relocate $12345
    nop
    .endrelocate
