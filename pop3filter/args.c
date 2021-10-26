#include <stdio.h>  /* for printf */
#include <stdlib.h> /* for exit */
#include <limits.h> /* LONG_MIN et al */
#include <string.h> /* memset */
#include <errno.h>
#include <getopt.h>

#include "./include/args.h"
#include "../utils/include/netutils.h"

static unsigned short port(const char *s)
{
    char *end = 0;
    const long sl = strtol(s, &end, 10);

    if (end == s || '\0' != *end || ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno) || sl < 0 || sl > USHRT_MAX)
    {
        fprintf(stderr, "port should in in the range of 1-65536: %s\n", s);
        exit(1);
        return 1;
    }
    return (unsigned short)sl;
}

static void version(void)
{
    fprintf(stderr, "pop3filter version 0.0\n"
                    "ITBA Protocolos de Comunicacion 2020/1 -- Grupo X\n"
                    "AQUI VA LA LICENCIA\n");
}

static void usage(const char *progname)
{
    fprintf(stderr,
            "Usage: %s [OPTION]...\n"
            "\n"
            "   -e <FILE PATH> Especifica el archivo donde se redirecciona stderr de las ejecuciones de los filtros.\n"
            "   -h                 Imprime la ayuda y termina.\n"
            "   -l <POP3 addr>     Direccion donde servira el proxy POP3.\n"
            "   -L <POP3 addr>     Direccion donde servira el servicio de management.\n"
            "   -o <POP3 port>     Puero donde esta el servicio de management. Default: 9090\n"
            "   -p <POP3 port>     Puerto entrante conexiones POP3. Default: 1110\n"
            "   -p <POP3 port>     Puerto dondde se encuentra el servidor POP3 en el servidor origen. Default: 110\n"
            "   -t <cmd>           Comando utilizado para transformaciones externas.\n"
            "   -v                 Imprime informacion sobre la version y termina.\n"
            "\n",
            progname);
    exit(1);
}

void parse_args(const int argc, const char **argv, struct proxyArgs *args, addressInfo *address)
{

    address->port = 1080;

    int optionArg;
    while (true)
    {
        optionArg = getopt(argc, (char *const *)argv, "e:hl:L:o:p:P:t:v");

        if (optionArg == -1)
            break;

        switch (optionArg)
        {
        case 'e':
            args->stderr_file_path = optarg;
            break;
        case 'h':
            usage(argv[0]);
            break;
        case 'l':
            args->proxy_addr = optarg;
            break;
        case 'L':
            args->listen_pop3_admin_address = optarg;
            break;
        case 'o':
            break;
        case 'p':
            address->port = port(optarg);
            break;
        case 'P':
            break;
        case 't':
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
    if (optind < argc)
    {
        fprintf(stderr, "argument not accepted: ");
        while (optind < argc)
        {
            fprintf(stderr, "%s ", argv[optind++]);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
}