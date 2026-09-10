; Long runs of fill, which is the case the short ones in fill.s and org.s do
; not reach.
;
; Every reservation here is written by one call, and a run of a thousand bytes
; goes through a different path from a run of three -- see emit_fill. What
; matters is that the length, the byte and where the run ends come out the
; same at any size, so the reference decides it as usual.
;
; FILLBYTE comes first because nothing may reserve space above it.

  fillbyte 0x5A
  ORG 0x040000
start:
  nop

; An ORG gap of two kilobytes. Kept to that, because the file is assembled on
; every test run.
  ORG 0x040800
  DB 1

; A reservation longer than any buffer.
  ds 1000
  DB 2

; Two reservations running together. The second continues the first rather
; than starting a new one, and the pair is one run of two thousand.
  ds 1000
  ds 1000
  DB 3

; A block, which is written where it lands rather than reserved.
  blkb 300, 0x7E
  DB 4

; ALIGN pads to the moved origin the same way.
  ALIGN 256
  DB 5

; A long block at each width, so the fill byte is not the only thing that
; varies with the count.
  blkw 200, 0x1234
  blkp 150, 0x123456
  DB 6

; And a reservation that reaches the end of the file, which is dropped: these
; bytes are not in the output at all.
  ds 900
