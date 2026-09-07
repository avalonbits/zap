; INCLUDE and INCBIN. Paths are opened exactly as written -- relative to where
; the assembler runs, not to the file doing the including, which is what the
; reference does and was measured rather than assumed.

  DB 0x01
  INCLUDE "test/cases/inc/bytes.inc"
  DB 0x02

; Two deep.
  INCLUDE "test/cases/inc/nested.inc"

; The same file twice, which has to open and close cleanly both times.
  INCLUDE "test/cases/inc/bytes2.inc"
  INCLUDE "test/cases/inc/bytes2.inc"

; A label defined inside one, referred to outside it.
  ld hl, inner

; And a label used inside one, defined outside: the fixup list does not care
; which file a reference came from.
  INCLUDE "test/cases/inc/withcode.inc"

; Raw bytes, which need no reader at all.
  INCBIN "test/cases/inc/blob.bin"
  DB 0x03
blob2:
  INCBIN "test/cases/inc/blob.bin"
  ld hl, blob2
  ld hl, $

; Lower case and the leading dot the reference takes on both.
  include "test/cases/inc/bytes2.inc"
  .INCLUDE "test/cases/inc/bytes2.inc"
  incbin "test/cases/inc/blob.bin"
  .INCBIN "test/cases/inc/blob.bin"
