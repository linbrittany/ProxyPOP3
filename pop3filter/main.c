#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

// #include "./include/args.h"
#include "netutils.h"
#include "selector.h"
#include "pop3nio.h"

#define BACKLOG 250
#define BUFFER_SIZE 4000

extern struct proxy_args args;

static address_info proxy_addr;
static address_info admin_proxy_addr;
static address_info origin_addr_data;

//Sockets tienen que ser gloabales
static int proxy = -1;
static int admin_proxy = -1;

static bool done = false;

static void sigterm_handler(const int signal) {
    printf("signal %d, cleaning up and exiting\n", signal);
    done = true;
    if(proxy != -1) close(proxy);
    if(admin_proxy != -1) close(admin_proxy);
}

static void set_up_proxy_args(void) {
    args.listen_pop3_address = "0.0.0.0";
    args.listen_pop3_admin_address = "127.0.0.1";
    args.stderr_file_path = "/dev/null";
    args.buffer_size = BUFFER_SIZE;
}

static unsigned short port(const char *s) {
    char *end = 0;
    const long sl = strtol(s, &end, 10);

    if (end == s || '\0' != *end || ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno) || sl < 0 || sl > USHRT_MAX){
        fprintf(stderr, "port should in in the range of 1-65536: %s\n", s);
        exit(1);
    }
    return (unsigned short)sl;
}

static void version(void) {
    fprintf(stderr, "pop3filter version 0.0\n"
                    "ITBA Protocolos de Comunicacion 2020/1 -- Grupo X\n"
                    "AQUI VA LA LICENCIA\n");
}

static void usage(const char *progname) {
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

void parse_args(const int argc, const char **argv) {

    admin_proxy_addr.port = 9090;
    proxy_addr.port = 1110;
    origin_addr_data.port = 110;

    int option_arg;
    while (true) {
        option_arg = getopt(argc, (char *const *)argv, "e:hl:L:o:p:P:t:v");

        if (option_arg == -1)
            break;

        switch (option_arg) {
            case 'e':
                args.stderr_file_path = optarg;
                break;
            case 'h':
                usage(argv[0]);
                break;
            case 'l':
                args.listen_pop3_address = optarg;
                break;
            case 'L':
                args.listen_pop3_admin_address = optarg;
                break;
            case 'o':
                admin_proxy_addr.port = port(optarg);
                break;
            case 'p':
                proxy_addr.port = port(optarg);
                break;
            case 'P':
                origin_addr_data.port = port(optarg);
                break;
            case 't':
                break;
            case 'v':
                version();
                exit(0);
            default:
                fprintf(stderr, "unknown argument %d.\n", option_arg);
                exit(1);
        }
    }
    if (argc - optind != 1) {
        fprintf(stderr, "argument not accepted: ");
        while (optind < argc) {
            fprintf(stderr, "%s ", argv[optind++]);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
    args.listen_origin_address = (char *)argv[optind];
}

int main(int argc, char const **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    
    memset(&args, 0, sizeof(args));
    set_up_proxy_args();
    parse_args(argc, argv);

    set_address(&proxy_addr, args.listen_pop3_address);
    set_address(&admin_proxy_addr, args.listen_pop3_admin_address);

    close(0); //Nada que leer de stdin

    const char *err_msg = NULL;

    if ((proxy = socket(proxy_addr.domain, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        err_msg = "socket() failed for proxy";
        exit(1);
    }

    if ((admin_proxy = socket(admin_proxy_addr.domain, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        err_msg = "socket() failed for proxy admin";
        exit(1);
    }

    selector_status ss = SELECTOR_SUCCESS;
    fd_selector selector = NULL;

    int ans = -1;

    if ((ans = setsockopt(proxy, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int))) < 0) {
        err_msg = "setsockopt() failed for proxy";
        close(proxy);
        exit(1);
    }

    if ((ans = setsockopt(admin_proxy, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int))) < 0) {
        err_msg = "setsockopt() failed for proxy admin";
        close(proxy);
        exit(1);
    }

    if((ans = bind(proxy, (struct sockaddr *)&proxy_addr.addr.storage, proxy_addr.addr_len)) < 0) {
        err_msg = "bind() failed for proxy";
        close(proxy);
        printf("%d",errno);
        exit(1);
    }

    if((ans = bind(admin_proxy, (struct sockaddr *)&admin_proxy_addr.addr.storage, admin_proxy_addr.addr_len)) < 0) {
        err_msg = "bind() failed for proxy admin";
        close(admin_proxy);
        exit(1);
    }

    if ((ans = listen(proxy, BACKLOG)) < 0) {
        err_msg = "listen() failed for proxy";
        printf("%s %d\n",err_msg,errno);
        close(proxy);
        exit(1);
    }

    if ((ans = listen(admin_proxy, BACKLOG)) < 0) {
        err_msg = "listen() failed for proxy admin";
        printf("%s %d\n",err_msg,errno);
        close(admin_proxy);
        exit(1);
    }

    printf("listening on tcp port: %d\n", proxy_addr.port);

    //Para terminar el programa correctamente
    signal(SIGTERM, sigterm_handler); //kill
    signal(SIGINT, sigterm_handler); //ctrl c

    if (selector_fd_set_nio(proxy) == -1) {
        err_msg = "getting server proxy flags";
        goto finally;
    }

    const struct selector_init conf = {
       .signal = SIGALRM,
       .select_timeout = {
           .tv_sec = 10,
           .tv_nsec = 0,
       },
    };

    if (selector_init(&conf) != 0) {
        err_msg = "Initializing selector";
        goto finally;
    }

    selector = selector_new(1024); 
    if (selector == NULL) {
        err_msg = "unable to create selector";
        goto finally;
    }

    const struct fd_handler pop3 = {
        .handle_read = pop3_passive_accept,
        .handle_write = NULL,
        .handle_close = NULL, // nada que liberar
    };

    const struct fd_handler pop3_admin = {
        .handle_read = NULL,
        .handle_write = NULL,
        .handle_close = NULL, // nada que liberar
    };

    set_address(&origin_addr_data, args.listen_origin_address);

    //Agregar origin addr al selector register del proxy
    if ((ss = selector_register(selector, proxy, &pop3, OP_READ, &origin_addr_data)) != SELECTOR_SUCCESS) {
        err_msg = "registering fd for proxy";
        goto finally;
    }

    if ((ss = selector_register(selector, admin_proxy, &pop3_admin, OP_READ, NULL)) != SELECTOR_SUCCESS) {
        err_msg = "registering fd for proxy admin";
        goto finally;
    }

    for (; !done;) {
        err_msg = NULL;
        ss = selector_select(selector);
        if (ss != SELECTOR_SUCCESS) {
            err_msg = "serving";
            goto finally;
        }
    }
    if (err_msg == NULL) {
        err_msg = "closing";
    }

    int ret = 0;

finally:
    if (ss != SELECTOR_SUCCESS) {
        fprintf(stderr, "%s: %s\n", (err_msg == NULL) ? "" : err_msg, ss == SELECTOR_IO ? strerror(errno) : selector_error(ss));
        ret = 2;
    }
    else if (err_msg) {
        perror(err_msg);
        ret = 1;
    }
    if (selector != NULL) {
        selector_destroy(selector);
    }
    selector_close();

//    socksv5_pool_destroy();

    if(proxy != -1) close(proxy);
    if(admin_proxy != -1) close(admin_proxy);

    return ret;
}
