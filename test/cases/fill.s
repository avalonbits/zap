; FILLBYTE: what a reservation is filled with, in place of 0xFF, and what a
; BLK with no fill of its own writes. It stands for the rest of the file.
;
; Nothing reserves space above it, and that is deliberate. In the reference a
; reservation is a gap filled when the file is written out, so the last
; FILLBYTE wins for all of them -- `ds 2 / fillbyte 0xAA` fills that earlier
; reservation too. One pass writes the bytes where it meets them, and
; reproducing that means remembering every reserved range to go back over. It
; is refused instead; test/run.sh checks the message.

  fillbyte 0xAA
  nop
  ds 3
  align 4
  blkb 3
  blkw 2
  blkp 2
  nop

; A BLK with a fill of its own ignores it.
  blkb 3, 0x55
  blkw 2, 0x1234
  nop

; It takes an expression, not only a literal, and the same value twice is not
; a change.
fillval: EQU 0xAA
  fillbyte fillval
  fillbyte 0x55 + 0x55
  nop
  ds 3
  nop

; And only the low byte of it is used.
  fillbyte 0x12AA
  nop
  ds 2

; A fill that names something still ahead. It is one value repeated n times,
; so there is nothing a per-byte fixup could usefully do: the run is written
; where it belongs and filled in when the value is known. The count is a
; different matter and is still refused -- how many bytes there are decides
; where everything after them lands.
  blkb 2, fillahead
  blkw 2, fillahead
  blkp 2, fillahead
  blkb 2, fillahead+1
  blkb 2, fillahead*2
fillahead: EQU 0x40
