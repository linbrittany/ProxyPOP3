#ifndef HELLO__PARSER_H_
#define HELLO__PARSER_H_

#include <stdint.h>
#include <stdbool.h>

#include "buffer.h"

enum hello_state {
    hello_message,
    hello_end_message,
    hello_crlf,
    hello_done,
    hello_error,
};

struct hello_parser {
    /******** zona privada *****************/
    enum hello_state state;
    /* metodos que faltan por leer */
    uint8_t remaining;
};

/** inicializa el parser */
void hello_parser_init (struct hello_parser *p);

/** entrega un byte al parser. retorna true si se llego al final  */
enum hello_state hello_parser_feed (struct hello_parser *p, uint8_t b);

/**
 * por cada elemento del buffer llama a `hello_parser_feed' hasta que
 * el parseo se encuentra completo o se requieren mas bytes.
 *
 * @param errored parametro de salida. si es diferente de NULL se deja dicho
 *   si el parsing se debió a una condición de error
 */
enum hello_state hello_consume(buffer *b, struct hello_parser *p, bool *errored);

/**
 * Permite distinguir a quien usa hello_parser_feed si debe seguir
 * enviando caracters o no. 
 *
 * En caso de haber terminado permite tambien saber si se debe a un error
 */
bool hello_is_done(const enum hello_state state, bool *errored);

#endif
