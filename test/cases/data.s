; The data directives: DB, DW, DL, DS and ALIGN, with every spelling the
; reference takes. Compared against it line by line, so this file holds only
; what it accepts; the refusals and the two deliberate divergences are unit
; tests in test_encode.c.

  DB 1
  db 2
  .DB 3
  DEFB 4
  BYTE 5
  ASCII 6

; A list, and the expression forms a value may take.
  DB 1, 2, 3
  DB 0x41, 65, 'A'
  DB 1+2*3
  DB 0xFF & 0x3C
  DB -1
  DB 256

; Strings, with the escapes the reference knows. Not \0 and not \x41, both of
; which it calls an illegal escape code.
  DB "hi"
  DB "hi", 0
  DB "a\"b"
  DB "back\\slash"
  DB "n\nt\tr\ra\ab\bf\fv\ve\e"
  DB "quote\'"
  DB "one", "two", 3
  ASCII "text"

; Two bytes and three, and their spellings.
  DW 0x1234
  DEFW 0x1234
  .DW 0x1234
  DW 1, 2, 3
  DL 0x123456
  DW24 0x123456
  .DL 0x123456
  DL 1, 2

; Reserved space, which the reference fills with 0xFF. The arguments after the
; count are taken and ignored there, so they are here too.
  DS 4
  DEFS 2
  BLKB 1
  DS 3, 0
  DS 0
  DS 1+2

; Alignment, which pads with 0xFF to the next multiple. The argument has to be
; a positive power of two.
  DB 1
  ALIGN 2
  DB 2
  ALIGN 4
  DB 3
  ALIGN 16
  DB 4
  ALIGN 1
  DB 5

; A label on the same line as data, and one above it.
tbl:  DB 1, 2, 3
tbl2:
  DB 4, 5, 6
  ld hl, tbl
  ld hl, tbl2

; Values that are not known yet. Each becomes a fixup of the directive's own
; width, which is the machinery an instruction's immediate already used.
  DB ahead
  DW ahead
  DL ahead
  DB ahead + 1
  DW end - start
start:
  nop
  nop
end:
ahead:
  DB 9

; And values that are known: an EQU, the program counter, a label behind.
val:  EQU 0x37
  DB val
  DW val * 2
  DB $ - start
  DL start

; Locals and anonymous labels reach data the same way they reach code.
scope:
@d:
  DB 7, 8
  ld hl, @d
@@:
  DB 9
  ld hl, @b

; A name as a data item, which is read by the atom directly rather than through
; the evaluator. What the short read must get right is everything the token
; could be besides a label: a trailing-h hex literal that begins with a letter,
; a local, an anonymous reference, the current address, and anything with an
; operator after it, which has to go the long way round.
dataname:
  DL dataname
  DW dataname
  DB dataname
  DL dataname , dataname,dataname
  DL dataname ; a remark after the item
  DL aabbcch
  DL abch
  DW 0ffh
  DL $
  DW $
  DL %1010
  DL dataname+1
  DL 1+dataname
  DL dataname-dataname
  DL -dataname
  DB "text", dataname, 3
  DL dataahead
  DW dataahead, 1
@dataloc:
  DL @dataloc
  DW @dataloc
@@:
  DL @b
  DL @f
@@:
dataequ: EQU 5
  DL dataequ
  DB dataequ
dataahead:
  nop

; BLKB, BLKW and BLKP: n units of a fill, written out.
;
; Not the same directive as DS, which is what they were mapped to here. DS
; reserves 0xFF, ignores any fill argument, and a run of it at the end of a
; file is dropped; these write the fill they are given and are never dropped.
; Getting that wrong made `blkb 1, 1` come out as 0xFF and `blkb 1, 255` at the
; end of a file come out as nothing.
;
; The default fill is 0xFF at the unit width and not all ones: `blkw 1` is
; FF 00, which is the value 0x00FF written as a word.
  blkb 3
  blkb 3, 0xAA
  blkb 3, 0
  blkb 0
  blkb 3, 0x1234
  blkb 3, -1
  blkw 2, 0x1234
  blkw 2
  blkw 1, 65535
  blkw 1, -32768
  blkp 2, 0x123456
  blkp 1
  blkp 1, 16777215
  blkp 1, -8388608
  .blkb 2, 0xAA
  BLKB 2, 0xAA
  blkb 2, 0xAA, 9
  blkb 2, 128+127-255+4/2*2<<1>>1&0x04|0x04
blkcount: EQU 3
blkfill:  EQU 0xAA
  blkb blkcount
  blkb 2, blkfill
  blkb 2, 1+1

; A block after a reservation is what stops the reservation being dropped, so
; the DS above it is written out and the DS below it is not.
  ds 3
  blkb 3
  ds 3
