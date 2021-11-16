#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "capa_parser.h"
#include "logger.h"

static const char *pipelining_msg = "PIPELINING\r\n";
static const size_t pipelining_len = 12;
static const char *crlf_msg = "\r\n.\r\n";
static const size_t crlf_len = 5;

void capa_parser_init(capa_parser *parser, capabilities *capa_list)
{
    parser->status = CAPA_PARSE_MSG;
    parser->curr_token.pipelining_size = 0;
    parser->capa_list = capa_list;
}

capa_state capa_parser_feed(struct capa_parser *parser, const uint8_t byte)
{
    switch (parser->status)
    {
        case CAPA_PARSE_MSG:
            if (byte == crlf_msg[0])
            {
                parser->status = CAPA_PARSE_CRLF;
                parser->curr_token.crlf_size = 1;
            }
            else if (byte == pipelining_msg[0])
            {
                parser->status = CAPA_PARSE_PIPELINING;
                parser->curr_token.pipelining_size = 1;
            }
            /*Si parsee \r\n y el caracter es un '.' */
            else if (byte == crlf_msg[2] && parser->curr_token.crlf_size++ == 2)
            {
                parser->status = CAPA_PARSE_CRLF;
                parser->curr_token.crlf_size = 3;
            }
            break;
        case CAPA_PARSE_PIPELINING:
            if (byte != pipelining_msg[parser->curr_token.pipelining_size])
                parser->status = CAPA_PARSE_MSG;
            else if (parser->curr_token.pipelining_size == pipelining_len - 1)
            {
                parser->status = CAPA_PARSE_CRLF;
                parser->capa_list->pipelining = true;
                parser->curr_token.crlf_size = 2;
            }
            else
                parser->curr_token.pipelining_size++;
            break;

        case CAPA_PARSE_CRLF:
            if (byte != crlf_msg[parser->curr_token.crlf_size] && parser->curr_token.crlf_size == 2)
            {
                if (byte == pipelining_msg[0])
                {
                    parser->status = CAPA_PARSE_PIPELINING;
                    parser->curr_token.pipelining_size = 1;
                }
                else
                    parser->status = CAPA_PARSE_MSG;
            }
            else if (byte != crlf_msg[parser->curr_token.crlf_size])
                parser->status = CAPA_PARSE_ERROR;
            else if (parser->curr_token.crlf_size == crlf_len - 1)
                parser->status = CAPA_PARSE_DONE;
            else
                parser->curr_token.crlf_size++;
            break;

        case CAPA_PARSE_DONE:
        case CAPA_PARSE_ERROR:
            // nada que hacer, nos quedamos en este estado
            break;
        default:
            log(ERR, "Error parsing CAPABILITIES %d\n", 1);
    }
    log(INFO, "status %d\n", parser->status);
    return parser->status;
}

capa_state capa_parser_consume(capa_parser *parser, buffer *buffer, bool *errored)
{
    capa_state status;
    while (buffer_can_read(buffer))
    {
        uint8_t byte = buffer_read(buffer);
        status = capa_parser_feed(parser, byte);
        if (capa_parser_done(status, errored))
            break;
    }
    log(INFO, "status en consume %d\n", status);
    return status;
}

bool capa_parser_done(const capa_state status, bool *errored)
{
    bool ret;
    switch (status)
    {
    case CAPA_PARSE_ERROR:
        if (0 != errored)
        {
            *errored = true;
        }
        /* no break */
    case CAPA_PARSE_DONE:
        ret = true;
        break;
    default:
        ret = false;
        break;
    }
    return ret;
}