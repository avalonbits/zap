; RELOCATE moves the addresses and leaves the bytes where they are, which is
; how code is assembled to run somewhere other than where it is written -- a
; routine copied into RAM, most often.
;
; `org` is what every address is measured from, so displacing it is the whole
; of the directive: labels, `$`, an EQU that takes `$`, and every fixup follow
; without knowing anything about it.
;
; The reference takes the spelling with and without the leading dot, and this
; file uses both.

  .org 0x40000
start:
  jp start

  .align 0x100
  .relocate 0x50000
relstart:
  jp $
  jp relstart
  jp relahead
@rellocal:
  jp @rellocal
  DL $
relahead:
  DL $
releq: EQU $
  DL releq
  jp start          ; a label from outside the block keeps its own address
  .endrelocate

; Back to where the bytes actually are, and the addresses go with them.
outside:
  DL $
  jp outside
  jp relstart       ; and a label from inside keeps its relocated one

  relocate 0x60000
  jp $
  DL $
  endrelocate
  DL $

; The assembler is eZ80 only, so .CPU is a check rather than a setting.
  .cpu ez80
  cpu EZ80
  .CPU eZ80
