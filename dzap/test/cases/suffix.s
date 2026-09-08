; Instruction mode suffixes: .SIS, .LIS, .SIL, .LIL, and the one- and
; two-letter spellings that name one half and leave the other as it was.
;
; A suffix puts one byte in front of the instruction -- in front of the DD or
; FD an index register brings, too -- and decides how wide the instruction's
; immediates and addresses are, whatever the ADL mode says.
;
;     .sis  0x40   short instruction, short immediates
;     .lis  0x49   long instruction, short immediates
;     .sil  0x52   short instruction, long immediates
;     .lil  0x5B   long instruction, long immediates
;
; The short spellings resolve against the mode: `.s` is `.sil` in ADL mode and
; `.sis` out of it. All eight are in the corpus, and `rst.lil` alone is 115 of
; the uses -- it is what MOSCALL expands to, which is why nothing that calls
; MOS assembled at all before this.

  assume adl = 1

; All four, and the width each gives the immediate.
  ld.sis hl, 0x1234
  ld.lis hl, 0x1234
  ld.sil hl, 0x1234
  ld.lil hl, 0x1234

; The short forms, in ADL mode.
  ld.s hl, 0x1234
  ld.l hl, 0x1234
  ld.is hl, 0x1234
  ld.il hl, 0x1234

; Case is not significant, in either half.
  LD.LIL HL, 0x123456
  Ld.LiL hl, 0x123456
  ld.LIL hl, 0x123456

; In front of the index prefix, not after it.
  ld.lil ix, 0x123456
  ld.sis iy, 0x1234
  bit.lil 0, (ix+5)
  bit.sis 7, (iy-4)

; The shapes that take one: memory, the stack, control flow, block moves. Not
; every row takes all four -- `retn.lil` assembles and `retn.sis` is "Suffix
; not matching mnemonic / ADL mode" -- which is why the table carries a mask
; per row rather than one bit, and why this is checked after the row is chosen
; rather than while it is being chosen.
  rst.lil 8
  rst.sis 0x38
  push.lil bc
  pop.sis de
  inc.lil hl
  dec.lil sp
  add.lil hl, bc
  res.lil 0, (hl)
  set.sis 3, (hl)
  ret.lil
  ret.lis
  reti.lil
  retn.lil
  ldir.lil
  lddr.sis
  cpir.lil
  ex.lil (sp), hl
  lea.lil hl, ix+5
  pea.lil ix+5
  jp.lil 0x123456
  jp.sis 0x1234
  jp.lil (hl)
  call.lis 0x1234
  call.lil 0x123456
  ld.lil a, (0x123456)
  ld.sis a, (0x1234)
  ld.lil (0x123456), hl
  ld.lil (hl), a
  ld.lil sp, 0x123456

; A label as the address, resolved backwards and forwards.
here:
  jp.lil here
  call.lil onward
  ld.lil hl, onward
  ld.sis hl, here
onward:
  nop

; And out of ADL mode, where the short spellings mean the other thing.
  assume adl = 0
  ld.s hl, 0x1234
  ld.l hl, 0x1234
  ld.is hl, 0x1234
  ld.il hl, 0x123456
  ld hl, 0x1234
  ld.lil hl, 0x123456
  assume adl = 1
