; An IF of its own, opened and closed inside this file while the caller
; has one open too.
  IF 1
  db 0x77
  ELSE
  db 0x88
  ENDIF
