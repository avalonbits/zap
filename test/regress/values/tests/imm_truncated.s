; The truncation boundaries, which are a warning in both and a refusal in
; neither: the bytes have to be identical for the message to be the only
; difference.
    .assume adl=1
    .org $40000
    ld a, 255
    ld a, -1
    ld a, 256
    ld a, -129
    dw 65535
    dw 65536
    dl 16777216
    dw24 0x1234567
