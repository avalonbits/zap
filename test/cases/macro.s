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

; A global label defers the end of its scope to the next local, and an
; expansion in between must not spend it. The body counts its own line numbers,
; so the deferral inside it never matches; before it was saved across the call,
; the local in the body ended the caller's scope in the caller's name, and the
; local below reported "label defined twice" against the scope above.
first:
@twice:
  nop
second:
  withlocal
@twice:
  nop

; Arguments are trimmed of the space around them, down to the last one.
;
; A single character is the case that matters, and it is the one that broke:
; the trim used to walk back from the end of the argument, which the compiler
; gets wrong at -Oz, and `1` came out empty while `65` came out whole. The
; benchmark sources are what exercise that on the target -- isa_real invokes
; `mload` with a one-digit argument -- and this pins the trimming itself.
  MACRO trim1 v
  db "[v]"
  ENDMACRO
  trim1 5
  trim1   5
  trim1 5  
  trim1   5  
  MACRO trim2 v, w
  db "[v][w]"
  ENDMACRO
  trim2 5, 6
  trim2   5  ,   65  

; A body with more local labels than the undo log starts with, invoked twice.
;
; Entering an expansion advances the generation stamp, which makes every bucket
; read as empty; leaving it puts the stamp back. What the stamp cannot undo is
; a bucket the body wrote over, so those are recorded as they happen, and the
; record grows -- the reference assembles a body with sixty locals in it, so a
; fixed table would refuse a legal program.
wide:
@k1:
  nop
  MACRO manylocals
@k1:
  nop
@k2:
  nop
@k3:
  nop
@k4:
  nop
@k5:
  nop
@k6:
  nop
@k7:
  nop
@k8:
  nop
@k9:
  nop
@k10:
  nop
@k11:
  nop
@k12:
  nop
@k13:
  nop
@k14:
  nop
@k15:
  nop
@k16:
  nop
@k17:
  nop
@k18:
  nop
@k19:
  nop
@k20:
  nop
@k21:
  nop
@k22:
  nop
@k23:
  nop
@k24:
  nop
@k25:
  nop
@k26:
  nop
@k27:
  nop
@k28:
  nop
@k29:
  nop
@k30:
  nop
@k31:
  nop
@k32:
  nop
@k33:
  nop
@k34:
  nop
@k35:
  nop
@k36:
  nop
@k37:
  nop
@k38:
  nop
@k39:
  nop
@k40:
  nop
@k41:
  nop
@k42:
  nop
@k43:
  nop
@k44:
  nop
@k45:
  nop
@k46:
  nop
@k47:
  nop
@k48:
  nop
@k49:
  nop
@k50:
  nop
@k51:
  nop
@k52:
  nop
@k53:
  nop
@k54:
  nop
@k55:
  nop
  jp @k55
  ENDMACRO
  manylocals
  manylocals
  jp @k1

; Parameters are matched case-sensitively, and everything else about a macro is
; not. `MACRO m v` with `V` in the body is "Unknown identifier" in the
; reference, and was a substitution here until it was measured -- the name, the
; directive and the invocation are all case-blind, so this had been assumed to
; be as well.
  MACRO casepar v, V
  db v
  db V
  ENDMACRO
  casepar 1, 2

; Two parameters that share a first letter, either way round, so that the
; cheap compare in front of the full one cannot decide it alone.
  MACRO sharefirst aa, ab
  db aa, ab
  ENDMACRO
  sharefirst 3, 4
  MACRO sharefirst2 ab, aa
  db aa, ab
  ENDMACRO
  sharefirst2 5, 6

; And the macro name itself, which is case-blind in both.
  MACRO MixedName q
  db q
  ENDMACRO
  mixedNAME 7

; A parameter name may begin with a digit without being a number. The
; reference's own corpus has one -- `0123456789abcdef0123456789abcdef`, which
; is not a number in any radix -- and it was never substituted here, because
; the substitution scan took a digit as the start of a number rather than of a
; token. A token that really is a number matches no parameter name and is
; copied through as it was.
  MACRO digitpar 0123456789abcdef0123456789abcdef
  db 0123456789abcdef0123456789abcdef
  db 0x1F
  db 10
  ENDMACRO
  digitpar 5

; And one that begins with a digit and ends like a radix suffix, which is a
; name and not a literal because nothing parses it as one.
  MACRO digitpar2 0abcz
  db 0abcz
  ENDMACRO
  digitpar2 7

; Where the parameters are is decided when the body is read, not when it is
; expanded, so these are the shapes that tell a mark list from a scan.

; The same parameter twice on one line, and once on the next: the marks are
; walked in body order across the whole body, so a cursor that did not carry
; between lines would put the second line back to the start of the list.
  MACRO twice a, b
  db a, a, b
  db b, a
  ENDMACRO
  twice 1, 2

; Adjacent, with nothing between them but a comma, so two marks meet with an
; empty span in between.
  MACRO adj p, q
  db p,q
  ENDMACRO
  adj 3, 4

; A parameter at the very start of a line and at the very end of one, which
; are the two spans that can be empty.
  MACRO edges e
  db e
  db 1, e
  ENDMACRO
  edges 9

; A body long enough to make the buffer it is copied into grow past its first
; block, which is 256 bytes. The marks are offsets for exactly this: the body
; is realloc'd as it is read, and a pointer taken before the move would be
; wrong afterwards.
  MACRO grow g
  db g, g, g, g, g, g, g, g, g, g, g, g, g, g, g, g
  db g, g, g, g, g, g, g, g, g, g, g, g, g, g, g, g
  db g, g, g, g, g, g, g, g, g, g, g, g, g, g, g, g
  db g, g, g, g, g, g, g, g, g, g, g, g, g, g, g, g
  db g, g, g, g, g, g, g, g, g, g, g, g, g, g, g, g
  db g, g, g, g, g, g, g, g, g, g, g, g, g, g, g, g
  ENDMACRO
  grow 2

; An argument longer than the parameter it replaces, and one shorter, so the
; expansion both grows and shrinks against the body it came from.
  MACRO widths wide
  db wide
  ENDMACRO
  widths 0x11223344 & 0xFF
  widths 1
