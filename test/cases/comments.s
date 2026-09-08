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

; A remark that is the last thing in the file, with no newline after it, and
; a remark that is the last line of a buffer refill. Both are where a scan
; that walks the body with its own cursor and writes the result back at the
; end could put that cursor in the wrong place -- the loop stops on the
; sentinel rather than on a real newline, and the line after it is the one
; that would show it.
  nop     ; trailing remark before the end
  ld a, 1 ;
  ld b, 2 ;;;;
  nop
;