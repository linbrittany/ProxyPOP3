#ifndef ARGS_H
#define ARGS_H

#include <stdbool.h>
#include "../../utils/include/netutils.h"

struct proxy_args
{
    int socketO;
    int socketC;
    char *stderr_file_path;
    char *listen_pop3_admin_address;
    char *listen_pop3_address;
    unsigned short  admin_port;
    unsigned short  pop3_port;
};


/**
 * Interpreta la linea de comandos (argc, argv) llenando
 * args con defaults o la seleccion humana. Puede cortar
 * la ejecucion.
 */
void parse_args(const int argc, const char **argv, struct proxy_args *args);

#endif
