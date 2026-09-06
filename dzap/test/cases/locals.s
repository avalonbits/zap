; Local labels, which belong to the global label above them.
;
; The reference keys one as the enclosing global's name with the local's
; appended, so the same spelling under two globals is two different labels and
; a reference from outside the scope finds nothing. Everything here is a case
; where that distinction shows in the bytes.
;
; No @b, @f, @n or @p: those two-character spellings are the reference's
; anonymous-label references whatever a local of that name would mean, and
; dzap refuses them rather than reading them as locals.

; Before any global label at all: the file is the scope.
@start:
  nop
  jp @start

first:
@loop:
  ld a, b
  djnz @loop
  jp @loop

; Forward, so the reference is recorded and settled when the scope ends.
  jp @tail
  ld hl, @tail
@tail:
  nop

second:
; The same spellings again. Different scope, different labels, and the
; addresses are what say so.
@loop:
  ld c, d
  jp @loop
@tail:
  jp @tail

; A global may be spelled like a local's name without the at sign, and the two
; do not collide.
loop:
  jp loop

third:
; Locals are not tested against the number formats, which globals are: `123:`
; and `0ffh:` are refused as globals and accepted here.
@123:
  nop
  jp @123
@0ffh:
  jp @0ffh

; The at sign is an ordinary name character anywhere but the first position.
mid@dle:
  jp mid@dle
