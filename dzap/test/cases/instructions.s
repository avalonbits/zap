; Every instruction shape dzap claims to handle, assembled by both dzap and
; ez80asm and compared byte for byte. The unit tests pin the cases a refactor
; is likely to break; this pins the breadth.
  nop
  halt
  ccf
  scf
  neg
  ldir
  lddr
  di
  ei
  ret
  reti
  retn
  exx
  daa
  cpl
  rla
  rra
  rlca
  rrca

; registers
  ld a, b
  ld c, d
  ld e, h
  ld l, a
  ld b, (hl)
  ld (hl), c
  add a, b
  adc a, c
  sub d
  sbc a, e
  and h
  xor l
  or a
  cp b
  inc a
  dec b
  inc hl
  dec sp
  add hl, de
  adc hl, bc
  sbc hl, hl
  ex de, hl
  push af
  push bc
  pop de
  pop iy
  ld sp, hl
  ld a, i
  ld i, a
  ld a, r
  mlt bc
  mlt de
  mlt hl
  mlt sp

; immediates, one byte and three
  ld a, 0x42
  ld b, 255
  ld c, 0
  cp 0x7F
  and 0xAA
  or 0x55
  xor 0x0F
  add a, 1
  sub 2
  ld bc, 0x1234
  ld de, 0x8000
  ld hl, 0x123456
  ld ix, 0x0400
  ld iy, 0x040000
  ld sp, 0x0BFFFF
  ld hl, 0x1
  ld hl, 0x123
  ld hl, 0x12345
  ld hl, 0xabcdef
  ld hl, 0xABCDEF
  ld de, 0xff00ff
  ld hl, 65535
  ld hl, 1000000
  ld a, -1

; indirection and index displacement
  ld a, (hl)
  ld a, (bc)
  ld a, (de)
  ld (hl), 0x99
  ld a, (ix+0)
  ld a, (ix+8)
  ld a, (ix-1)
  ld a, (iy+127)
  ld b, (iy-128)
  ld (ix+8), a
  ld (iy-4), b
  add a, (ix+2)
  adc a, (ix+0)
  sub (iy+1)
  inc (ix+3)
  dec (iy+5)
  ld a, (0x040100)
  ld hl, (0x040200)
  ld (0x040300), a
  ld (0x040400), hl

; bit operations
  bit 0, b
  bit 3, (iy+4)
  bit 7, (hl)
  res 0, c
  res 7, b
  set 0, (hl)
  set 5, d
  rlc d
  rrc e
  rl h
  rr l
  sla e
  sra a
  srl c

; control flow with condition codes
  ret nz
  ret z
  ret nc
  ret c
  ret po
  ret pe
  ret p
  ret m
  jp 0x040000
  jp z, 0x040000
  jp nz, 0x040000
  jp (hl)
  call 0x040000
  call nc, 0x040000
  rst 0x00
  rst 0x18
  rst 0x38
  im 0
  im 1
  im 2

; ports
  out (0xFE), a
  in a, (0xFE)
  out (c), b
  in d, (c)
