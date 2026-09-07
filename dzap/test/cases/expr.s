; Expressions, which the reference evaluates strictly left to right with no
; precedence at all. 1+2*3 is 9 here and 7 nowhere.
;
; Grouping is [...] rather than parentheses, because parentheses already mean
; indirection: (hl) is a memory operand, [1+2] is arithmetic.

  ld hl, 1+2
  ld hl, 1+2*3
  ld hl, 2*3+1
  ld hl, 1+[2*3]
  ld hl, [1+2]*3
  ld hl, [[1+2]*3]
  ld hl, 10-2-3
  ld hl, 100/7
  ld hl, 1<<4
  ld a, 8>>2
  ld hl, 0xFF&0x0F
  ld hl, 1|2|4
  ld hl, 0xFF^0x0F
  ld hl, -5+10
  ld hl, ~0
  ld hl, 5 + 3
  ld a, 'A'
  ld a, 'A'+1
  ld a, 'A'*2

; The address of the instruction being assembled. The same character starts a
; hex literal, so $ alone is the address and $42 is a number.
  ld hl, $
  ld hl, $+4
  ld hl, $42
  ld hl, $-$

; Labels of every kind as terms, so long as they are defined already.
start:
  nop
  nop
mid:
  ld hl, start+2
  ld hl, mid-start
  ld hl, [mid-start]*4
  ld a, mid&0xFF
  ld a, [mid>>8]&0xFF
  ld hl, start+2*3

@loc:
  ld hl, @loc+1
  ld hl, [@loc-start]+1

@@:
  ld hl, @b+2
  ld hl, [@b-start]*2

; Expressions in the places an immediate can go, not just ld.
  ld a, 2+3
  add a, 1+1
  cp 0x40|0x01
  bit [1+2], a
  ld (mid+1), a
  jp mid+0

; Forward references with a constant. The fixup carries a symbol and an addend,
; so a label that has not appeared yet can still have something added to it --
; which is what most real expressions over a label look like.
  jp fwd+3
  ld hl, fwd-1
  ld hl, 4+fwd
  ld a, fwd+1
  ld hl, [fwd]+2
  ld hl, fwd+1-1
  ld hl, fwd-start
fwd:
  nop

; Two forward references at once. `end - start` with neither written yet is
; how a program measures a table it is still emitting, so both slots of the
; fixup are used and one of them is subtracted.
  ld hl, f2-f1
  ld hl, f1-f2
  ld hl, f2-f1+4
  ld hl, 4+f2-f1
  ld hl, f1+f2
  ld bc, f2-f1
  ld a, f2-f1
  jp f2-f1
  ld hl, [f2-f1]+1
  ld hl, -f1+f2
f1:
  nop
  nop
  nop
f2:
  nop
