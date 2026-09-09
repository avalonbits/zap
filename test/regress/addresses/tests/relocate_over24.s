; Refused by both. RELOCATE is the directive the reference does range-check.
    .assume adl=1
    .org $40000
    .relocate $1000000
    nop
    .endrelocate
