#ifndef PARSER_MULTILINE_H
#define PARSER_MULTILINE_H
#include "buffer.h"
#include "selector.h"
#include "pop3nio.h"
enum filter_states {
    HEADER,
    HEADER_CR_1,
    HEADER_N,
    HEADER_CR_2,
    NEW_LINE,
    DOT,
    DOT_2,
    DOT_CR,
    BYTE,
    CR,

    // estados terminales
    END,
};

extern int state;
extern int state_out;

int parse_headers(struct copy * c);
void back_to_pop3(char * read);

#endif
