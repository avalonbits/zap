; Macros. MACRO name [params], a body, ENDMACRO, and then the name as an
; instruction. Any case, with or without the leading dot.
;
; Substitution is textual and by whole identifier, which was measured rather
; than assumed: with `x` bound to `1+1` the reference assembles `db 10-x` as
; ten and not eight, and leaves `xy` alone.

  MACRO plain
  nop
  ENDMACRO

  MACRO one x
  ld a, x
  ENDMACRO

  MACRO two x, y
  ld a, x
  ld b, y
  ENDMACRO

  macro lower
  ret
  endmacro

  .MACRO dotted
  ccf
  .ENDMACRO

; Invocation, more than once, and with a label in front.
  plain
  plain
here:  plain
  ld hl, here

  one 5
  one 0x41
  one -1
  one (hl)
  two 5, 6
  lower
  dotted

; Textual, so the argument goes in as it was written and binds where it lands.
  MACRO textual x
  db 10-x
  db 2*x
  ENDMACRO
  textual 1+1

; A whole identifier and not raw text: this parameter is `x` and the body names
; `xy`, which is a different thing.
xy:  EQU 7
  MACRO longer x
  db xy
  db x
  ENDMACRO
  longer 5

; A body may hold a local label, and every expansion gets its own scope -- so
; the same name twice is not a redefinition, and it cannot be named afterwards.
  MACRO withlocal
@spin:
  nop
  jp @spin
  ENDMACRO
  withlocal
  withlocal

; And a macro may invoke another, as long as that one is defined by the time
; the outer one is used.
  MACRO inner
  db 0xAA
  ENDMACRO
  MACRO outer
  inner
  db 0xBB
  ENDMACRO
  outer

; The expansion gets a scope of its own without taking the caller's away. All
; three of these were wrong when the body simply ended the surrounding scope on
; its way in and out: the caller's locals were resolved and cleared by the
; invocation, so a name defined on either side of it went missing.
caller:
@before:
  nop
  plain
  jp @before
  jp @after
@after:
  nop

; The same name in both scopes is two different labels, and the one the caller
; sees is its own.
shadow:
@same:
  nop
  MACRO shadowing
@same:
  ccf
  ENDMACRO
  shadowing
  jp @same

; Which holds however deep the nesting goes.
nest:
@deep:
  nop
  MACRO innermost
@deep:
  ccf
  ENDMACRO
  MACRO outermost
@deep:
  scf
  innermost
  jp @deep
  ENDMACRO
  outermost
  jp @deep
