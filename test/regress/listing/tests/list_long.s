; A listing long enough to fill the listing buffer several times over.
;
; The other files in this group produce between 387 and 1,561 bytes of
; .lst, and the buffer behind list_out is a kilobyte -- so none of them
; flushes it more than once and none straddles it. This one is about
; twelve kilobytes, so the boundary falls in the middle of a line a dozen
; times, and the forward references at the top are patched by
; lstfix_apply long after the rows holding them were written out and
; forgotten.
;
; No macros and no reservations: those are the two things a listing here
; may not contain. See the note in test/corpus.sh.
    .assume adl=1
    .org $40000
head:
    ld hl, tail
    call tail
    dl tail
    dw32 tail
l000:
    ld a, $00
    ld bc, $0000
    db $00, $00, $00, $00, $00
    ld hl, tail
    dw tail
    jp l000
    nop
l001:
    ld b, $01
    ld de, $0025
    nop
l002:
    ld c, $02
    ld hl, $004A
    nop
l003:
    ld d, $03
    ld bc, $006F
    nop
l004:
    ld e, $04
    ld de, $0094
    nop
l005:
    ld h, $05
    ld hl, $00B9
    db $05, $0A, $0F, $14, $19
    nop
l006:
    ld l, $06
    ld bc, $00DE
    nop
l007:
    ld a, $07
    ld de, $0103
    nop
l008:
    ld b, $08
    ld hl, $0128
    nop
l009:
    ld c, $09
    ld bc, $014D
    nop
l010:
    ld d, $0A
    ld de, $0172
    db $0A, $14, $1E, $28, $32
    nop
l011:
    ld e, $0B
    ld hl, $0197
    nop
l012:
    ld h, $0C
    ld bc, $01BC
    nop
l013:
    ld l, $0D
    ld de, $01E1
    nop
l014:
    ld a, $0E
    ld hl, $0206
    nop
l015:
    ld b, $0F
    ld bc, $022B
    db $0F, $1E, $2D, $3C, $4B
    nop
l016:
    ld c, $10
    ld de, $0250
    nop
l017:
    ld d, $11
    ld hl, $0275
    ld hl, tail
    dw tail
    nop
l018:
    ld e, $12
    ld bc, $029A
    nop
l019:
    ld h, $13
    ld de, $02BF
    nop
l020:
    ld l, $14
    ld hl, $02E4
    db $14, $28, $3C, $50, $64
    nop
l021:
    ld a, $15
    ld bc, $0309
    nop
l022:
    ld b, $16
    ld de, $032E
    nop
l023:
    ld c, $17
    ld hl, $0353
    jp l023
    nop
l024:
    ld d, $18
    ld bc, $0378
    nop
l025:
    ld e, $19
    ld de, $039D
    db $19, $32, $4B, $64, $7D
    nop
l026:
    ld h, $1A
    ld hl, $03C2
    nop
l027:
    ld l, $1B
    ld bc, $03E7
    nop
l028:
    ld a, $1C
    ld de, $040C
    nop
l029:
    ld b, $1D
    ld hl, $0431
    nop
l030:
    ld c, $1E
    ld bc, $0456
    db $1E, $3C, $5A, $78, $96
    nop
l031:
    ld d, $1F
    ld de, $047B
    nop
l032:
    ld e, $20
    ld hl, $04A0
    nop
l033:
    ld h, $21
    ld bc, $04C5
    nop
l034:
    ld l, $22
    ld de, $04EA
    ld hl, tail
    dw tail
    nop
l035:
    ld a, $23
    ld hl, $050F
    db $23, $46, $69, $8C, $AF
    nop
l036:
    ld b, $24
    ld bc, $0534
    nop
l037:
    ld c, $25
    ld de, $0559
    nop
l038:
    ld d, $26
    ld hl, $057E
    nop
l039:
    ld e, $27
    ld bc, $05A3
    nop
l040:
    ld h, $28
    ld de, $05C8
    db $28, $50, $78, $A0, $C8
    nop
l041:
    ld l, $29
    ld hl, $05ED
    nop
l042:
    ld a, $2A
    ld bc, $0612
    nop
l043:
    ld b, $2B
    ld de, $0637
    nop
l044:
    ld c, $2C
    ld hl, $065C
    nop
l045:
    ld d, $2D
    ld bc, $0681
    db $2D, $5A, $87, $B4, $E1
    nop
l046:
    ld e, $2E
    ld de, $06A6
    jp l046
    nop
l047:
    ld h, $2F
    ld hl, $06CB
    nop
l048:
    ld l, $30
    ld bc, $06F0
    nop
l049:
    ld a, $31
    ld de, $0715
    nop
l050:
    ld b, $32
    ld hl, $073A
    db $32, $64, $96, $C8, $FA
    nop
l051:
    ld c, $33
    ld bc, $075F
    ld hl, tail
    dw tail
    nop
l052:
    ld d, $34
    ld de, $0784
    nop
l053:
    ld e, $35
    ld hl, $07A9
    nop
l054:
    ld h, $36
    ld bc, $07CE
    nop
l055:
    ld l, $37
    ld de, $07F3
    db $37, $6E, $A5, $DC, $13
    nop
l056:
    ld a, $38
    ld hl, $0818
    nop
l057:
    ld b, $39
    ld bc, $083D
    nop
l058:
    ld c, $3A
    ld de, $0862
    nop
l059:
    ld d, $3B
    ld hl, $0887
    nop
l060:
    ld e, $3C
    ld bc, $08AC
    db $3C, $78, $B4, $F0, $2C
    nop
l061:
    ld h, $3D
    ld de, $08D1
    nop
l062:
    ld l, $3E
    ld hl, $08F6
    nop
l063:
    ld a, $3F
    ld bc, $091B
    nop
l064:
    ld b, $40
    ld de, $0940
    nop
l065:
    ld c, $41
    ld hl, $0965
    db $41, $82, $C3, $04, $45
    nop
l066:
    ld d, $42
    ld bc, $098A
    nop
l067:
    ld e, $43
    ld de, $09AF
    nop
l068:
    ld h, $44
    ld hl, $09D4
    ld hl, tail
    dw tail
    nop
l069:
    ld l, $45
    ld bc, $09F9
    jp l069
    nop
l070:
    ld a, $46
    ld de, $0A1E
    db $46, $8C, $D2, $18, $5E
    nop
l071:
    ld b, $47
    ld hl, $0A43
    nop
l072:
    ld c, $48
    ld bc, $0A68
    nop
l073:
    ld d, $49
    ld de, $0A8D
    nop
l074:
    ld e, $4A
    ld hl, $0AB2
    nop
l075:
    ld h, $4B
    ld bc, $0AD7
    db $4B, $96, $E1, $2C, $77
    nop
l076:
    ld l, $4C
    ld de, $0AFC
    nop
l077:
    ld a, $4D
    ld hl, $0B21
    nop
l078:
    ld b, $4E
    ld bc, $0B46
    nop
l079:
    ld c, $4F
    ld de, $0B6B
    nop
l080:
    ld d, $50
    ld hl, $0B90
    db $50, $A0, $F0, $40, $90
    nop
l081:
    ld e, $51
    ld bc, $0BB5
    nop
l082:
    ld h, $52
    ld de, $0BDA
    nop
l083:
    ld l, $53
    ld hl, $0BFF
    nop
l084:
    ld a, $54
    ld bc, $0C24
    nop
l085:
    ld b, $55
    ld de, $0C49
    db $55, $AA, $FF, $54, $A9
    ld hl, tail
    dw tail
    nop
l086:
    ld c, $56
    ld hl, $0C6E
    nop
l087:
    ld d, $57
    ld bc, $0C93
    nop
l088:
    ld e, $58
    ld de, $0CB8
    nop
l089:
    ld h, $59
    ld hl, $0CDD
    nop
l090:
    ld l, $5A
    ld bc, $0D02
    db $5A, $B4, $0E, $68, $C2
    nop
l091:
    ld a, $5B
    ld de, $0D27
    nop
l092:
    ld b, $5C
    ld hl, $0D4C
    jp l092
    nop
l093:
    ld c, $5D
    ld bc, $0D71
    nop
l094:
    ld d, $5E
    ld de, $0D96
    nop
l095:
    ld e, $5F
    ld hl, $0DBB
    db $5F, $BE, $1D, $7C, $DB
    nop
l096:
    ld h, $60
    ld bc, $0DE0
    nop
l097:
    ld l, $61
    ld de, $0E05
    nop
l098:
    ld a, $62
    ld hl, $0E2A
    nop
l099:
    ld b, $63
    ld bc, $0E4F
    nop
l100:
    ld c, $64
    ld de, $0E74
    db $64, $C8, $2C, $90, $F4
    nop
l101:
    ld d, $65
    ld hl, $0E99
    nop
l102:
    ld e, $66
    ld bc, $0EBE
    ld hl, tail
    dw tail
    nop
l103:
    ld h, $67
    ld de, $0EE3
    nop
l104:
    ld l, $68
    ld hl, $0F08
    nop
l105:
    ld a, $69
    ld bc, $0F2D
    db $69, $D2, $3B, $A4, $0D
    nop
l106:
    ld b, $6A
    ld de, $0F52
    nop
l107:
    ld c, $6B
    ld hl, $0F77
    nop
l108:
    ld d, $6C
    ld bc, $0F9C
    nop
l109:
    ld e, $6D
    ld de, $0FC1
    nop
l110:
    ld h, $6E
    ld hl, $0FE6
    db $6E, $DC, $4A, $B8, $26
    nop
l111:
    ld l, $6F
    ld bc, $100B
    nop
l112:
    ld a, $70
    ld de, $1030
    nop
l113:
    ld b, $71
    ld hl, $1055
    nop
l114:
    ld c, $72
    ld bc, $107A
    nop
l115:
    ld d, $73
    ld de, $109F
    db $73, $E6, $59, $CC, $3F
    jp l115
    nop
l116:
    ld e, $74
    ld hl, $10C4
    nop
l117:
    ld h, $75
    ld bc, $10E9
    nop
l118:
    ld l, $76
    ld de, $110E
    nop
l119:
    ld a, $77
    ld hl, $1133
    ld hl, tail
    dw tail
    nop
l120:
    ld b, $78
    ld bc, $1158
    db $78, $F0, $68, $E0, $58
    nop
l121:
    ld c, $79
    ld de, $117D
    nop
l122:
    ld d, $7A
    ld hl, $11A2
    nop
l123:
    ld e, $7B
    ld bc, $11C7
    nop
l124:
    ld h, $7C
    ld de, $11EC
    nop
l125:
    ld l, $7D
    ld hl, $1211
    db $7D, $FA, $77, $F4, $71
    nop
l126:
    ld a, $7E
    ld bc, $1236
    nop
l127:
    ld b, $7F
    ld de, $125B
    nop
l128:
    ld c, $80
    ld hl, $1280
    nop
l129:
    ld d, $81
    ld bc, $12A5
    nop
l130:
    ld e, $82
    ld de, $12CA
    db $82, $04, $86, $08, $8A
    nop
l131:
    ld h, $83
    ld hl, $12EF
    nop
l132:
    ld l, $84
    ld bc, $1314
    nop
l133:
    ld a, $85
    ld de, $1339
    nop
l134:
    ld b, $86
    ld hl, $135E
    nop
l135:
    ld c, $87
    ld bc, $1383
    db $87, $0E, $95, $1C, $A3
    nop
l136:
    ld d, $88
    ld de, $13A8
    ld hl, tail
    dw tail
    nop
l137:
    ld e, $89
    ld hl, $13CD
    nop
l138:
    ld h, $8A
    ld bc, $13F2
    jp l138
    nop
l139:
    ld l, $8B
    ld de, $1417
    nop
l140:
    ld a, $8C
    ld hl, $143C
    db $8C, $18, $A4, $30, $BC
    nop
l141:
    ld b, $8D
    ld bc, $1461
    nop
l142:
    ld c, $8E
    ld de, $1486
    nop
l143:
    ld d, $8F
    ld hl, $14AB
    nop
l144:
    ld e, $90
    ld bc, $14D0
    nop
l145:
    ld h, $91
    ld de, $14F5
    db $91, $22, $B3, $44, $D5
    nop
l146:
    ld l, $92
    ld hl, $151A
    nop
l147:
    ld a, $93
    ld bc, $153F
    nop
l148:
    ld b, $94
    ld de, $1564
    nop
l149:
    ld c, $95
    ld hl, $1589
    nop
l150:
    ld d, $96
    ld bc, $15AE
    db $96, $2C, $C2, $58, $EE
    nop
l151:
    ld e, $97
    ld de, $15D3
    nop
l152:
    ld h, $98
    ld hl, $15F8
    nop
l153:
    ld l, $99
    ld bc, $161D
    ld hl, tail
    dw tail
    nop
l154:
    ld a, $9A
    ld de, $1642
    nop
l155:
    ld b, $9B
    ld hl, $1667
    db $9B, $36, $D1, $6C, $07
    nop
l156:
    ld c, $9C
    ld bc, $168C
    nop
l157:
    ld d, $9D
    ld de, $16B1
    nop
l158:
    ld e, $9E
    ld hl, $16D6
    nop
l159:
    ld h, $9F
    ld bc, $16FB
    nop
l160:
    ld l, $A0
    ld de, $1720
    db $A0, $40, $E0, $80, $20
    nop
l161:
    ld a, $A1
    ld hl, $1745
    jp l161
    nop
l162:
    ld b, $A2
    ld bc, $176A
    nop
l163:
    ld c, $A3
    ld de, $178F
    nop
l164:
    ld d, $A4
    ld hl, $17B4
    nop
l165:
    ld e, $A5
    ld bc, $17D9
    db $A5, $4A, $EF, $94, $39
    nop
l166:
    ld h, $A6
    ld de, $17FE
    nop
l167:
    ld l, $A7
    ld hl, $1823
    nop
l168:
    ld a, $A8
    ld bc, $1848
    nop
l169:
    ld b, $A9
    ld de, $186D
    nop
l170:
    ld c, $AA
    ld hl, $1892
    db $AA, $54, $FE, $A8, $52
    ld hl, tail
    dw tail
    nop
l171:
    ld d, $AB
    ld bc, $18B7
    nop
l172:
    ld e, $AC
    ld de, $18DC
    nop
l173:
    ld h, $AD
    ld hl, $1901
    nop
l174:
    ld l, $AE
    ld bc, $1926
    nop
l175:
    ld a, $AF
    ld de, $194B
    db $AF, $5E, $0D, $BC, $6B
    nop
l176:
    ld b, $B0
    ld hl, $1970
    nop
l177:
    ld c, $B1
    ld bc, $1995
    nop
l178:
    ld d, $B2
    ld de, $19BA
    nop
l179:
    ld e, $B3
    ld hl, $19DF
    nop
l180:
    ld h, $B4
    ld bc, $1A04
    db $B4, $68, $1C, $D0, $84
    nop
l181:
    ld l, $B5
    ld de, $1A29
    nop
l182:
    ld a, $B6
    ld hl, $1A4E
    nop
l183:
    ld b, $B7
    ld bc, $1A73
    nop
l184:
    ld c, $B8
    ld de, $1A98
    jp l184
    nop
l185:
    ld d, $B9
    ld hl, $1ABD
    db $B9, $72, $2B, $E4, $9D
    nop
l186:
    ld e, $BA
    ld bc, $1AE2
    nop
l187:
    ld h, $BB
    ld de, $1B07
    ld hl, tail
    dw tail
    nop
l188:
    ld l, $BC
    ld hl, $1B2C
    nop
l189:
    ld a, $BD
    ld bc, $1B51
    nop
l190:
    ld b, $BE
    ld de, $1B76
    db $BE, $7C, $3A, $F8, $B6
    nop
l191:
    ld c, $BF
    ld hl, $1B9B
    nop
l192:
    ld d, $C0
    ld bc, $1BC0
    nop
l193:
    ld e, $C1
    ld de, $1BE5
    nop
l194:
    ld h, $C2
    ld hl, $1C0A
    nop
l195:
    ld l, $C3
    ld bc, $1C2F
    db $C3, $86, $49, $0C, $CF
    nop
l196:
    ld a, $C4
    ld de, $1C54
    nop
l197:
    ld b, $C5
    ld hl, $1C79
    nop
l198:
    ld c, $C6
    ld bc, $1C9E
    nop
l199:
    ld d, $C7
    ld de, $1CC3
    nop
l200:
    ld e, $C8
    ld hl, $1CE8
    db $C8, $90, $58, $20, $E8
    nop
l201:
    ld h, $C9
    ld bc, $1D0D
    nop
l202:
    ld l, $CA
    ld de, $1D32
    nop
l203:
    ld a, $CB
    ld hl, $1D57
    nop
l204:
    ld b, $CC
    ld bc, $1D7C
    ld hl, tail
    dw tail
    nop
l205:
    ld c, $CD
    ld de, $1DA1
    db $CD, $9A, $67, $34, $01
    nop
l206:
    ld d, $CE
    ld hl, $1DC6
    nop
l207:
    ld e, $CF
    ld bc, $1DEB
    jp l207
    nop
l208:
    ld h, $D0
    ld de, $1E10
    nop
l209:
    ld l, $D1
    ld hl, $1E35
    nop
l210:
    ld a, $D2
    ld bc, $1E5A
    db $D2, $A4, $76, $48, $1A
    nop
l211:
    ld b, $D3
    ld de, $1E7F
    nop
l212:
    ld c, $D4
    ld hl, $1EA4
    nop
l213:
    ld d, $D5
    ld bc, $1EC9
    nop
l214:
    ld e, $D6
    ld de, $1EEE
    nop
l215:
    ld h, $D7
    ld hl, $1F13
    db $D7, $AE, $85, $5C, $33
    nop
l216:
    ld l, $D8
    ld bc, $1F38
    nop
l217:
    ld a, $D9
    ld de, $1F5D
    nop
l218:
    ld b, $DA
    ld hl, $1F82
    nop
l219:
    ld c, $DB
    ld bc, $1FA7
    nop
l220:
    ld d, $DC
    ld de, $1FCC
    db $DC, $B8, $94, $70, $4C
    nop
l221:
    ld e, $DD
    ld hl, $1FF1
    ld hl, tail
    dw tail
    nop
l222:
    ld h, $DE
    ld bc, $2016
    nop
l223:
    ld l, $DF
    ld de, $203B
    nop
l224:
    ld a, $E0
    ld hl, $2060
    nop
l225:
    ld b, $E1
    ld bc, $2085
    db $E1, $C2, $A3, $84, $65
    nop
l226:
    ld c, $E2
    ld de, $20AA
    nop
l227:
    ld d, $E3
    ld hl, $20CF
    nop
l228:
    ld e, $E4
    ld bc, $20F4
    nop
l229:
    ld h, $E5
    ld de, $2119
    nop
l230:
    ld l, $E6
    ld hl, $213E
    db $E6, $CC, $B2, $98, $7E
    jp l230
    nop
l231:
    ld a, $E7
    ld bc, $2163
    nop
l232:
    ld b, $E8
    ld de, $2188
    nop
l233:
    ld c, $E9
    ld hl, $21AD
    nop
l234:
    ld d, $EA
    ld bc, $21D2
    nop
l235:
    ld e, $EB
    ld de, $21F7
    db $EB, $D6, $C1, $AC, $97
    nop
l236:
    ld h, $EC
    ld hl, $221C
    nop
l237:
    ld l, $ED
    ld bc, $2241
    nop
l238:
    ld a, $EE
    ld de, $2266
    ld hl, tail
    dw tail
    nop
l239:
    ld b, $EF
    ld hl, $228B
    nop
l240:
    ld c, $F0
    ld bc, $22B0
    db $F0, $E0, $D0, $C0, $B0
    nop
l241:
    ld d, $F1
    ld de, $22D5
    nop
l242:
    ld e, $F2
    ld hl, $22FA
    nop
l243:
    ld h, $F3
    ld bc, $231F
    nop
l244:
    ld l, $F4
    ld de, $2344
    nop
l245:
    ld a, $F5
    ld hl, $2369
    db $F5, $EA, $DF, $D4, $C9
    nop
l246:
    ld b, $F6
    ld bc, $238E
    nop
l247:
    ld c, $F7
    ld de, $23B3
    nop
l248:
    ld d, $F8
    ld hl, $23D8
    nop
l249:
    ld e, $F9
    ld bc, $23FD
    nop
tail:
    ld a, $5A
    ret
