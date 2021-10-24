#include <stdio.h>
#include <unistd.h>

#include "args.h"

static void
version(void) {
    fprintf(stderr, "pop3filter version 0.0\n"
                    "ITBA Protocolos de Comunicacion 2020/1 -- Grupo X\n"
                    "AQUI VA LA LICENCIA\n");
}

static void
usage(const char *progname) {
    fprintf(stderr,
        "Usage: %s [OPTION]...\n"
        "\n"
        "   -h               Imprime la ayuda y termina.\n"
        "   -l <POP3 addr>  Direccion donde servira el proxy POP3.\n"
        "   -p <POP3 port>  Puerto entrante conexiones POP3.\n"
        "   -v               Imprime informacion sobre la version y termina.\n"
        "\n",
        progname);
    exit(1);
}

void 
parseArgs(const int argc, char **argv, struct proxyArgs *args) {
    memset(args, 0, sizeof(*args)); // sobre todo para setear en null los punteros de users

    int optionArg;

    while (true) {

        optionArg = getopt(argc, argv, "");

        if (optionArg == -1)
            break;

        switch (optionArg) {
            case 'h':
                usage(argv[0]);
                break;
            case 'l':
                args->proxyAddr = optarg;
                break;
            case 'p':
                args->proxyPort = port(optarg);
                break;
            case 'v':
                version();
                exit(0);
                break;
            default:
                fprintf(stderr, "unknown argument %d.\n", optionArg);
                exit(1);
        }

    }
    if (optind < argc) {
        fprintf(stderr, "argument not accepted: ");
        while (optind < argc) {
            fprintf(stderr, "%s ", argv[optind++]);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
}