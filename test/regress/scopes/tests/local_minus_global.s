; A difference between a local label and a global one where both are still
; ahead of the line that subtracts them.
;
; This used to be a difference zap kept on purpose, and it is not one any
; more. A reference to a local goes on the scope's own list and is settled
; when the scope ends, because the node it points at is handed back there and
; the next scope's `@l` may be sitting in it; a global is settled at the end
; of the source. So a fixup naming one of each has to have its local half
; folded into the addend at the scope end and the rest left for later, which
; is what fold_subs does. The reference has read the file twice by the time it
; works any of this out.
;
; The three shapes that reach that code, all of them with every label still
; ahead of its use, and all three now byte-identical to the reference:
;
;   * a local minus a global, where the global is not defined until two scopes
;     further down -- so it is still unknown when the local's scope ends;
;   * a global minus a local, which is the shape fold_subs is named for;
;   * a local minus a local in the same scope, which needs neither.
    .assume adl=1
    .org $40000
g:
    dl @l - far
    dl far - @l
    dl @m - @l
@l:
    nop
@m:
    nop
g2:
    nop
far:
    nop
