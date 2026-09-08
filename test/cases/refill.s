; Lines that straddle the reader's refill boundary.
;
; The reader fills its buffer with whole lines and carries the partial line at
; the end forward to the next fill. A scan that runs off the end is stopped by
; a newline written one past the last valid byte -- and that byte is the first
; character of the carried line, so writing it there silently ate the first
; character of whatever line happened to straddle the boundary.
;
; Nothing shorter than the buffer can catch that. This file places a line
; across each of the first three boundaries, at a different offset each time,
; so a carry of 1, of about half a line, and of nearly a whole line all occur.
;
; Generated; do not edit by hand.
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
;xxxxx
; a remark long enough to straddle the boundary it is aimed at
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
;xxxxxxxxx
; a remark long enough to straddle the boundary it is aimed at
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
;xxxxxxxxx
; a remark long enough to straddle the boundary it is aimed at
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
  nop
  ld a, b
  inc hl
  ld (ix+8), a
  bit 3, (iy+4)
  ld bc, 0x1234
  push af
  ret nz
  halt
  ld hl, 0x123456
