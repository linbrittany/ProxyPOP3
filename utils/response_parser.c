#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "response_parser.h"
#include "logger.h"
#include "queue.h"

#define MAX_RSP_SIZE 512

static const char * crlf_inline = "\r\n";
static const size_t crlf_inline_size = 2;
static const char * crlf_multiline = "\r\n.\r\n";
static const size_t crlf_multiline_size = 5;
static const char * ok_indicator = "+OK";
static const size_t ok_indicator_size = 3;
static const char * err_indicator = "-ERR";
static const size_t err_indicator_size = 4;

void rsp_parser_init(struct rsp_parser * parser) {
    parser->state = RSP_INIT;
    parser->msg_size = 0;
    parser->line_size = 0;
}

extern enum rsp_state rsp_parser_feed(struct rsp_parser * parser, const uint8_t b, struct Queue * queue) {
    struct st_command * current_cmd = dequeue(queue);
    switch (parser->state) {
        case RSP_INIT:
            if (b == ok_indicator[parser->line_size]) {
                parser->state = RSP_INDICATOR_OK;
                current_cmd->indicator = true;
                parser->line_size++;
            } else if (b == err_indicator[parser->line_size]) {
                parser->state = RSP_INDICATOR_ERR;
                current_cmd->indicator = false;
                parser->line_size++;
            } else {
                parser->state = RSP_ERROR;
            }
            break;
        case RSP_INDICATOR_OK:
            if (b != ok_indicator[parser->line_size]) {
                parser->state = RSP_ERROR;
                break;
            } 
            if (parser->line_size == ok_indicator_size - 1) {
                parser->state = RSP_INDICATOR_BODY;
            }
            parser->line_size++;
            break;
        case RSP_INDICATOR_ERR:
            if (b != err_indicator[parser->line_size]) {
                parser->state = RSP_ERROR;
                break;
            } 
            if (parser->line_size == err_indicator_size - 1) {
                parser->state = RSP_INDICATOR_BODY;
            }
            parser->line_size++;
            break;    
        case RSP_INDICATOR_BODY:
            if (b == crlf_inline[0]) {
                parser->state = RSP_CRLF_INLINE;
                parser->check_crlf = 1;
            }
            break;
        case RSP_CRLF_INLINE:
            if (b == crlf_inline[parser->check_crlf++]) {
                if (parser->check_crlf == crlf_inline_size) {
                    if (current_cmd->is_multiline) {
                        parser->state = RSP_BODY;
                        parser->line_size = 0;
                    }
                }
            }
            else {
                parser->state = RSP_ERROR;
            }
            break;
        case RSP_BODY: 
            if(b == crlf_multiline[0]) {
                parser->check_crlf = 1;
            } else if (b == crlf_multiline[1]) {
                if (parser->check_crlf != 1) {
                    parser->state = RSP_ERROR;
                } else {
                    parser->line_size = 0;
                    parser->check_crlf++;
                }
            } else if (b == crlf_multiline[2] && parser->line_size == 0) {
                parser->state = RSP_CRLF_MULTILINE;
                parser->check_crlf++;
            } else {
                parser->line_size++;
            }
            break;
        case RSP_CRLF_MULTILINE:
            if (b == crlf_multiline[parser->check_crlf++]) {
                break;
            }
            if (parser->check_crlf == crlf_multiline_size - 1) {
                parser->state = RSP_BODY;
                parser->check_crlf = 0;
            }
            else {
                parser->state = RSP_ERROR;
            }
            break;
        case RSP_ERROR: 
            // nada que hacer, nos quedamos en este estado
            break;
        default:
            break;
    }
    if (parser->msg_size++ == MAX_RSP_SIZE) {
        parser->state = RSP_ERROR;
    }
    return parser->state;
}

extern enum rsp_state rsp_consume(buffer *b, struct rsp_parser *p, struct Queue *queue, bool *errored) {
    enum rsp_state st = p->state;
    while(buffer_can_parse(b)) {
        const uint8_t c = buffer_parse(b);
        st = rsp_parser_feed(p, c, queue);
        if (p->state == RSP_ERROR) {
            *errored = true;
            break;
        }
    }
    return st;
}
