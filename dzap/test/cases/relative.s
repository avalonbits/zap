; Relative jumps, whose displacement depends on where the instruction sits.
;
; dzap emitted the target address truncated to one byte for all of these until
; the TR_REL transform was implemented -- `jr 0x040000` assembled to 18 00 where
; the reference gives 18 fe. Nothing caught it: no benchmark or case file held a
; relative jump, and the corpus forms that did were dropped from opcodes.s for
; failing the very byte comparison that filter exists to enforce.
;
; The file is deliberately short. Every target here is an absolute address near
; the origin, and a displacement only reaches 127 bytes forward and 128 back, so
; a long file of these would go out of range rather than test anything. That is
; also why the generated benchmarks still contain none: without labels there is
; no way to write a target that stays in reach as the output grows.
  jr 0x040000
  jr nz, 0x040000
  jr z, 0x040000
  jr nc, 0x040000
  jr c, 0x040000
  djnz 0x040000

; Forward and backward from a moving address, with other instructions between
; so the displacement has to track the output growing.
  nop
  jr 0x040020
  ld a, b
  jr nz, 0x040000
  ld bc, 0x1234
  djnz 0x040008
  inc hl
  jr c, 0x04003C
  ld (ix+8), a
  jr 0x040000
  halt
