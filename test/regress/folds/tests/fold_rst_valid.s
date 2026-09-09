; The eight restart addresses: the multiples of eight below 0x40, which are
; exactly the values that fold into 0xC7 without disturbing a bit already in
; it. zap ORed anything, so `rst 0x09` was a working call to `rst 0x08`.
    .assume adl=1
    .org $40000
    rst 0
    rst 08h
    rst 10h
    rst 18h
    rst 20h
    rst 28h
    rst 30h
    rst 38h
