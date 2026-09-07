; ASSUME ADL, which decides whether an address-sized immediate is three bytes
; or two. ADL=1 is the eZ80's own mode and the one this starts in; ADL=0 is
; Z80 mode. A file may switch as often as it likes.

  ld hl, 0x1234          ; the default, before any ASSUME

  .assume adl=1
  ld hl, 0x1234
  jp 0x1234
  call 0x1234
  ld (0x1234), hl
  ld a, 5                ; a byte immediate is not an address and does not move

  .assume adl=0
  ld hl, 0x1234
  jp 0x1234
  call 0x1234
  ld (0x1234), hl
  ld a, 5

; Every spelling the reference takes.
  ASSUME ADL=1
  ld hl, 0x1234
  assume adl = 0
  ld hl, 0x1234
  .ASSUME ADL=1
  ld hl, 0x1234

; The value is read as a number, so this is one.
  .assume adl=01
  ld hl, 0x1234

; Labels and the program counter follow the width in force where they are used.
here:
  .assume adl=0
  ld hl, here
  ld hl, $
  .assume adl=1
  ld hl, here
  ld hl, $

; A forward reference records the width it had where it was written, not where
; the label turns up.
  .assume adl=0
  ld hl, ahead
  .assume adl=1
ahead:
  nop
