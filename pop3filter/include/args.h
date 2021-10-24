#ifndef ARGS_H
#define ARGS_H

#include <stdbool.h>

struct proxyArgs {
    char           *proxyAddr;
    unsigned short  proxyPort;
};

/**
 * Interpreta la linea de comandos (argc, argv) llenando
 * args con defaults o la seleccion humana. Puede cortar
 * la ejecucion.
 */
void 
parseArgs(const int argc, char **argv, struct proxyArgs *args);

#endif