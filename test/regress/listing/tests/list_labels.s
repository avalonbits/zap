; Lines that emit nothing still take a row: a label, an EQU, a directive that
; only moves the assembler along, and a comment.
;
; No ALIGN and no DS here, though both belong on a line like these. The
; reference lists the fill they generate on a continuation row with the first
; row left empty -- unlike the fill an ORG generates, which it lists inline --
; and zap lists both inline. Bytes agree; see values/fills.s, which pins them.
    .assume adl=1
    .org $40000
one: EQU 1
two: EQU 2
start:
; a whole-line comment
    nop            ; and a trailing one
@local:
    jr @local
    nop
