; Conditional assembly. IF, ELSE, ENDIF, in any case and with or without the
; leading dot.
;
; Nesting is not supported -- the reference says so and means it -- so this is
; one flag rather than a stack, and an IF inside an IF is an error in both.
;
; Compared against the reference, so the `==` here is the reference's: it
; evaluates the left side and throws the rest away. See test_encode.c for the
; two modes; this file is the compatible one.

  IF 1
  db 0x01
  ENDIF

  IF 0
  db 0x02
  ENDIF

  IF 1
  db 0x03
  ELSE
  db 0x04
  ENDIF

  IF 0
  db 0x05
  ELSE
  db 0x06
  ENDIF

; ELSE toggles, and toggles again.
  IF 1
  db 0x07
  ELSE
  db 0x08
  ELSE
  db 0x09
  ENDIF

; Every spelling.
  if 1
  db 0x0A
  endif
  .IF 1
  db 0x0B
  .ENDIF

; The condition is an expression, and any non-zero value is true.
val:  EQU 3
  IF val
  db 0x0C
  ENDIF
  IF val - 3
  db 0x0D
  ENDIF
  IF -1
  db 0x0E
  ENDIF
  IF 1 + 1
  db 0x0F
  ENDIF

; Nothing in a branch that is switched off happens: no label is defined, no
; value named, no ORG taken, and a name that is never defined is not looked up.
  IF 0
skipped_label:
  db ahead
  ORG 0x50000
  INCBIN "nosuch.bin"
  ENDIF
  db 0x10
ahead:
  db 0x11

; And the whole of it inside an included file works the same way.
  INCLUDE "test/cases/inc/bytes2.inc"

; A macro definition inside a branch that is not taken is skipped, not
; captured. Both of these were wrong: the first assembled the body from the
; branch that was not taken, and the second defined a macro the reference
; never sees.
  IF 1
  MACRO condtaken
  db 0x11
  ENDMACRO
  ELSE
  MACRO condtaken
  db 0x22
  ENDMACRO
  ENDIF
  condtaken

; And a macro invoked inside a branch that is not taken is not expanded.
  MACRO condskipped
  db 0x33
  ENDMACRO
  IF 0
  condskipped
  ENDIF
  db 0x44

; ASSUME takes an expression, not only a literal. `assume adl=one` with `one`
; an EQU is what the reference's own Labels corpus writes.
condone: EQU 1
condzero: EQU 0
  assume adl = condone
  ld hl, 0x1234
  assume adl = condzero
  ld hl, 0x1234
  assume adl = 0+1
  ld hl, 0x1234
