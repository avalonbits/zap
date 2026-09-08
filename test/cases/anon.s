; Anonymous labels: @@, written as often as you like and reached by position
; rather than by name -- @b and @p for the one above, @f and @n for the one
; below.
;
; What matters here is which address comes out. Every line assembles under a
; wrong answer too; only the bytes say whether @b found the nearer of two, or
; whether @f on a line that defines one means that one or the next.

; It takes effect at once, unlike a global label, whose scope starts on the
; next line. So this jumps to itself.
@@: jp @b

  jp @f
@@:
  nop
  jp @b

; The nearer one in each direction, and both spellings of each.
@@:
  nop
@@:
  jp @p
  jp @n
@@:
  ld hl, @b

; Several forward references land on the same one.
  jp @f
  call @f
  ld hl, @f
@@:
  nop

; Relative jumps, where the displacement is a byte and the fixup a different
; width from the others.
@@:
  jr @b
  jr @f
@@:
  nop

; Not name-scoped: neither global nor local labels disturb them, and they do
; not disturb the label tables either.
outer:
@loop:
@@:
  nop
  jp @b
  jp @loop
second:
@loop:
  jp @loop
  jp @b

; Only the exact two-character spellings are reserved. Three characters is an
; ordinary local, in the scope of the global above.
@ff:
  jp @ff
@bb:
  jp @bb
