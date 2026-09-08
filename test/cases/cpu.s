; .CPU: the instruction set the rest of the file is written in.
;
; A filter and not a second assembler. The row table has carried a CPU mask
; since it was written -- Z80, undocumented Z80, Z180, eZ80 -- and this
; directive chooses which bits a row has to overlap. Compared against the
; reference line by line, like every file here.
;
; The mode follows the machine: neither the Z80 nor the Z180 has ADL, so both
; select it off and an immediate is two bytes rather than three.

; The eZ80, which is the default and is what an Agon source is.
  ld hl, 0x1234
  ld ixh, b
  in0 a, (5)
  ld.lil hl, 0x123456

; The Z80, and its undocumented instructions -- the ones the eZ80 dropped.
.cpu Z80
  ld hl, 0x1234
  ld de, 0

  sll b
  sll c
  sll d
  sll e
  sll h
  sll l
  sll a
  sll (hl)
  sll (ix+0)
  sll (iy+3)

  in (c)
  out (c),0

  inc ixh
  inc ixl
  inc iyh
  inc iyl
  ld ixh, b
  ld a, iyl

; The undocumented DD CB forms, which write the result to a register as well
; as to memory. Every one of the eight shifts, on both index registers.
  rlc (ix+0),b
  rrc (ix+1),c
  rl  (ix+2),d
  rr  (ix+3),e
  sla (ix+4),h
  sra (ix+5),l
  sll (ix+6),a
  srl (ix+7),b
  rlc (iy-1),l
  srl (iy-2),a

; And the three-operand RES and SET, which are the only instructions in the
; reference with an operand after the second. The bit is part of the pseudo
; mnemonic the table holds, so this is two operands by the time a row sees it.
  res 0,(ix+0),b
  res 7,(ix+0),a
  res 3,(iy+2),d
  set 0,(ix+0),b
  set 7,(iy-4),a
  set 5,(ix+1),h
; Spaces around the commas, which the reference takes and gives the same bytes.
  set 5, (ix+1) , h

; The Z180, whose extra instructions the eZ80 also has.
.cpu Z180
  ld hl, 0x1234
  in0 b, (0)
  out0 (0),b
  otim
  otimr
  tst a,b
  tstio 0
  slp
  mlt bc

; And back to the eZ80, which restores ADL as well as the rows.
.cpu eZ80
  ld hl, 0x1234
  ld.lil hl, 0x123456
  ld ixh, b
