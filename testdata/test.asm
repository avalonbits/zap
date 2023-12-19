    .ASSUME ADL = 1
    .ORG $400000

    jp _start

    .ALIGN 64
    .db "MOS"
    .db 0
    .db 1


_start: LD      HL, hello_world ; Load string address to HL register.
        CALL    prstr           ; Call print  string function.
        LD      HL, 0           ; Set application return code to 0.
        RET                     ; Done.

prstr:  LD      A,(HL)          ; Load current character from string.
        OR      A               ; Check if it is zero. If it is the
        RET     Z               ; If zero, we are done.

        RST.LIL 10h             ; Send character to VDP.
        INC     HL              ; Go to next character.
        JR      prstr           ; Loop back.


hello_world:    .asciz "Hello, World!\r\n"