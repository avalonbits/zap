; The mode suffixes, which change the bytes and therefore the row.
    .assume adl=1
    .org $40000
    ld.sis hl, 0x1234
    ld.lil hl, 0x123456
    rst.lil 8
    ret.l
    jp.lil 0x123456
    call.is 0x1234
