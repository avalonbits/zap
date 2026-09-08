; A whole line that is nothing but a remark.
  nop                          ; and one after an instruction
;no space after the semicolon
  ld a, 0x42;no space before it either

; A blank comment line follows.
;
  ld (ix+8), a   ; a remark after an operand with a displacement
   ; an indented whole-line remark
  ret nz ; after a condition code
  halt
