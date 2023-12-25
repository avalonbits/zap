#ifndef _INSTRUCTION_PARSER_H
#define _INSTRUCTION_PARSER_H

struct _parser;

const char* parse_call(struct _parser* p);
const char* parse_jp(struct _parser* p);
const char* parse_ld(struct _parser* p);
const char* parse_or(struct _parser* p);
const char* parse_ret(struct _parser* p);
const char* parse_rst(struct _parser* p);

#endif  // _INSTRUCTION_PARSER_H
