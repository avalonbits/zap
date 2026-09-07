; ORG, which is two directives wearing one name.
;
; The first one in a file moves the origin and writes nothing. Every later one
; pads out to its address with 0xFF, and that is true even when nothing has
; been emitted in between -- two ORGs in a row write the gap between them.
; Measured against the reference, which is the only way to find that out.

  ORG 0x050000

; The origin moved, so this is where the program counter starts.
start:
  ld hl, start
  ld hl, $
  nop

; A second ORG pads. Kept small here because the file is assembled on every
; test run and the reference will happily write 64 KB of 0xFF.
  ORG 0x050010
after:
  ld hl, after
  DB $ - start

; ORG to exactly where the counter already is writes nothing.
  ORG 0x050015
  DB 1

; Alignment works off the moved origin, not off the output offset.
  ALIGN 16
  DB 2

; Labels, forward references and relative jumps all measure from the new
; origin. A relative is the one that would go wrong silently.
  jp ahead
  jr ahead
  jr $
ahead:
  nop
  DW ahead
  DL ahead

; Lower case and the reference's leading dot.
  .ORG 0x050100
  DB 3
  org 0x050110
  DB 4

; An origin that comes from a value rather than a literal.
where: EQU 0x050200
  ORG where
  ld hl, $
