#include <stdio.h>
#include <stdlib.h>

#include "hello_parser.h"

#define HELLO_MAX_MSG_SIZE 512

static const char * hello_positive_message = "+OK";
static const size_t hello_positive_message_size = 3;

static const char * crlf_message = "\r\n";

extern void hello_parser_init(struct hello_parser *p) {
   p->state = hello_message;
   p->remaining = 0;
}

extern enum hello_state hello_parser_feed(struct hello_parser *p, const uint8_t b) {
    switch(p->state) {
        case hello_indicator:
            if(b != hello_positive_message[p->remaining]) {
                p->state = hello_error;
            } else if (p->remaining == (hello_positive_message_size - 1)){
                p->state = hello_message;
            }
            break;
        case hello_message:
            if (b == crlf_message[0]) {
                p->state = hello_crlf;
            }
            break;
        case hello_crlf:
            if (b == crlf_message[1]) {
                p->state = hello_done;
            }
            else {
                p->state = hello_error;
            }
            break;
        case hello_done:
        case hello_error:
            // nada que hacer, nos quedamos en este estado
            break;
        default:
            fprintf(stderr, "unknown state %d\n", p->state);
            abort();
    }
    if(p->remaining++ == HELLO_MAX_MSG_SIZE)
        p->state = hello_error;
    return p->state;
}

extern bool hello_is_done(const enum hello_state state, bool *errored) {
    bool ret;
    switch (state) {
        case hello_error:
            if (0 != errored) {
                *errored = true;
            }
            /* no break */
        case hello_done:
            ret = true;
            break;
        default:
            ret = false;
            break;
    }
   return ret;
}

extern enum hello_state hello_consume(buffer *b, struct hello_parser *p, bool *errored) {
    enum hello_state st = p->state;
    while(buffer_can_parse(b)) {
        const uint8_t c = buffer_parse(b);
        if (b->parsed == b->write) {
            b->parsed = b->data;
        }
        st = hello_parser_feed(p, c);
        buffer_parse_reset(b);
        if (hello_is_done(st, errored)) {
            break;
        }
    }
    return st;
}
