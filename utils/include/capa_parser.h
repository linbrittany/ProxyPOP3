#include "buffer.h"

typedef enum capa_state {
    CAPA_PARSE_MSG        = 0,
    CAPA_PARSE_PIPELINING = 1,
    CAPA_PARSE_CRLF       = 2,
    CAPA_PARSE_DONE       = 3,
    CAPA_PARSE_ERROR      = 4,
} capa_state;

typedef struct capabilities {
    /*list of capabilities we are seeking at input*/
    bool pipelining;
} capabilities;

typedef struct capa_parser {
    capa_state      status;
    capabilities    *capa_list;
    union {
        size_t     crlf_size;
        size_t     pipelining_size;
    } curr_token;
} capa_parser;

/*alimenta al parser con un byte*/
capa_state capa_parser_feed(struct capa_parser  *parser, const uint8_t byte);

/*Consume de a un byte del buffer hasta que no tenga mas elementos*/
capa_state capa_parser_consume(capa_parser *parser, buffer *buffer, bool *errored);

/*retorna true si el parser consumio todo lo que tenia por consumir*/
bool capa_parser_done(const capa_state status, bool *errored);

void capa_parser_init(capa_parser *parser, capabilities *capa_list);

