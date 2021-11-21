#ifndef RESPONSE_PARSER_H
#define RESPONSE_PARSER_H

#include "command_parser.h"

enum rsp_state {
    RSP_INIT,
    RSP_INDICATOR_OK,
    RSP_INDICATOR_ERR,
    RSP_INDICATOR_BODY,
    RSP_BODY,
    RSP_CRLF_INLINE,
    RSP_CRLF_MULTILINE,
    RSP_ERROR,
};

struct rsp_parser {
    size_t msg_size;
    size_t line_size;
    size_t check_crlf;
    enum rsp_state state;
};

void rsp_parser_init(struct rsp_parser * parser);
enum rsp_state rsp_parser_feed (struct rsp_parser * parser, const uint8_t b, struct st_command * command);
enum rsp_state rsp_consume(buffer *b, struct rsp_parser *p, bool *errored);

#endif
