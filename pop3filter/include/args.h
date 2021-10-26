#ifndef ARGS_H
#define ARGS_H

#include <stdbool.h>
#include "../../utils/include/netutils.h"

struct proxyArgs
{
    char *stderr_file_path;
    char *listen_pop3_admin_address;
    char *proxy_addr;
};

/**
 * Interpreta la linea de comandos (argc, argv) llenando
 * args con defaults o la seleccion humana. Puede cortar
 * la ejecucion.
 */
void parse_args(const int argc, const char **argv, struct proxyArgs *args, addressInfo *address);

#endif
