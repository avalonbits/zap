; Instructions, labels and the features that came before, all inside an
; included file: the scope, the fixup list and the symbol table are the
; assembler's and not the file's.
scope_in_include:
@loc:
  nop
  jp @loc
  jp fwd_from_include
fwd_from_include:
  ld hl, scope_in_include
val_in_include: EQU 0x37
  DB val_in_include
  ALIGN 4
  DB 0xEE
