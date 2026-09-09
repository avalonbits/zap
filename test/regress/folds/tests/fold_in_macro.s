; A fold inside a macro body, where the value is the argument. The reference
; substitutes the text and then reads it, so this is the ordinary path with a
; different source of the number.
    .assume adl=1
    .org $40000
    MACRO setbit which, reg
    set which, reg
    ENDMACRO
    setbit 0, a
    setbit 7, b
    setbit 3, (hl)
