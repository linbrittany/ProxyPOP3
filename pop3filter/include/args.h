#ifndef ARGS_H
#define ARGS_H

#include <stdbool.h>
#include "../../utils/include/netutils.h"

struct proxyArgs {
    char *proxyAddr;
};

/**
 * Interpreta la linea de comandos (argc, argv) llenando
 * args con defaults o la seleccion humana. Puede cortar
 * la ejecucion.
 */
void 
parseArgs(const int argc, const char **argv, struct proxyArgs *args, addressInfo * address);

#endif