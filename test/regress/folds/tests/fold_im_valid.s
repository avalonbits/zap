; The three interrupt modes, and a negative one, which the reference takes as
; mode 0 rather than refusing.
    .assume adl=1
    .org $40000
    im 0
    im 1
    im 2
    im -1
    im -100
