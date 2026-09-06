; Case and whitespace are not part of the encoding.
  LD A, B
  Ld A, (Ix+8)
  ld a,b
   ld    a  ,   b
	ld	a,	b
  LD BC, 0X1234
  NOP
  RET NZ
  BIT 3, (IY+4)
