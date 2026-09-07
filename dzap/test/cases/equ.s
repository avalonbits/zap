; EQU: a name for a value rather than for an address.
;
; Every line here is one the reference assembles, so the whole file is compared
; against it. The forms it refuses -- a forward reference inside the value, a
; second definition, `X EQU 5` without the colon -- are in test_encode.c, where
; a refusal can be asserted.

FIVE:   EQU 5
lower:  equ 6
dotted: .EQU 7
neg:    EQU -1
wide:   EQU 0x123456
sum:    EQU FIVE + lower * 2
mask:   EQU 0xFF & 0x3C
chr:    EQU 'A'
remark: EQU 9        ; a comment after the value

; A name that is also a mnemonic, which the reference allows.
ld:     EQU 11

  ld a, FIVE
  ld a, lower
  ld a, dotted
  ld a, neg
  ld hl, wide
  ld a, sum
  ld a, mask
  ld a, chr
  ld a, remark
  ld a, ld

; Used where an immediate goes, not just in `ld`.
  cp FIVE
  bit FIVE-2, a
  ld (wide), a
  jp sum+0x040000

; A forward reference to an EQU. The value is not known where it is used, so it
; goes through the fixup list exactly like a label that has not appeared yet.
  ld hl, later
  ld a, later
later:  EQU 0x40

; An EQU whose value comes from a label already defined, and from the program
; counter.
here:
  nop
  nop
after:  EQU here
span:   EQU $ - here
  ld hl, after
  ld a, span

; Locals, which the reference takes too. A global label opens the scope.
scope:
@one:   EQU 21
@two:   EQU @one + 1
  ld a, @one
  ld a, @two
