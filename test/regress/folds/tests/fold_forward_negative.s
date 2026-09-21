; A bit number below zero, defined after the instruction that uses it.
;
; The reference's two answers for this are not the same, and which one you get
; depends on whether the value was known when the instruction was written.
; Written out, `bit -1, a` is masked into the opcode and assembles to cb ff.
; Named by a label defined further down, it goes through the fixup path, which
; checks both ends of the range and refuses it.
;
; So this file is refused by both assemblers and fold_bit_negative.s, the same
; value written out, is assembled by both. Neither is an accident and the pair
; is here so that changing one without the other is noticed.
    .assume adl=1
    .org $40000
    bit n, a
n:  EQU -1
