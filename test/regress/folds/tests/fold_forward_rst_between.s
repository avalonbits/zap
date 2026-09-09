; Refused by both when the label settles: 9 is not a restart address.
    .assume adl=1
    .org $40000
    rst vec
vec: EQU 9
