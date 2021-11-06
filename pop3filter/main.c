#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "./include/args.h"
#include "../utils/include/netutils.h"
#include "./include/selector.h"
#include "./include/pop3nio.h"

#define BACKLOG 250

static struct proxy_args args;


//static int proxy = -1;
//static int admin_proxy = -1;

static bool done = false;

static void sigterm_handler(const int signal) {
    printf("signal %d, cleaning up and exiting\n", signal);
    done = true;
}

static void set_up_proxy_args(void) {
    args.listen_pop3_address = "0.0.0.0";
    args.listen_pop3_admin_address = "127.0.0.1";
    args.stderr_file_path = "/dev/null";
}

int main(int argc, char const **argv) {
    memset(&args, 0, sizeof(args));
    set_up_proxy_args();
    parse_args(argc, argv, &args);

    int socketC = set_address((&args)->admin_port,args.listen_pop3_address);
    int socketO = set_address((&args)->admin_port,args.listen_pop3_admin_address);

    printf("%d",socketC);
   

    (&args)->socketC = socketC;

    (&args)->socketO= socketO;

    const char *err_msg = NULL;
    selector_status ss = SELECTOR_SUCCESS;
    fd_selector selector = NULL;

   int ans;


    if ((ans = listen(socketC, BACKLOG)) < 0)
    {
        err_msg = "listen() failed for proxy";
        close(socketC);
        exit(1);
    }

    if ((ans = listen(socketO, BACKLOG)) < 0)
    {
        err_msg = "listen() failed for proxy admin";
        close(socketO);
        exit(1);
    }

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);

    if (selector_fd_set_nio(socketC) == -1)
    {
        err_msg = "getting server proxy flags";
        goto finally;
    }
//    const struct selector_init conf = {
//        .signal = SIGALRM,
//        .select_timeout = {
//            .tv_sec = 10,
//            .tv_nsec = 0,
//        },
//    };

    selector = selector_new(1024);
    if (selector == NULL)
    {
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

    if ((ss = selector_register(selector, socketC, &pop3, OP_READ, NULL)) != SELECTOR_SUCCESS)
    {
        err_msg = "registering fd for proxy";
        goto finally;
    }

    if ((ss = selector_register(selector, socketO, &pop3_admin, OP_READ, NULL)) != SELECTOR_SUCCESS)
    {
        err_msg = "registering fd for proxy admin";
        goto finally;
    }

    for (; !done;)
    {
        err_msg = NULL;
        ss = selector_select(selector);
        if (ss != SELECTOR_SUCCESS)
        {
            err_msg = "serving";
            goto finally;
        }
    }
    if (err_msg == NULL)
    {
        err_msg = "closing";
    }

    int ret = 0;

finally:
    if (ss != SELECTOR_SUCCESS)
    {
        fprintf(stderr, "%s: %s\n", (err_msg == NULL) ? "" : err_msg, ss == SELECTOR_IO ? strerror(errno) : selector_error(ss));
        ret = 2;
    }
    else if (err_msg)
    {
        perror(err_msg);
        ret = 1;
    }
    if (selector != NULL)
    {
        selector_destroy(selector);
    }
    selector_close();

//    socksv5_pool_destroy();

    if (socketC>= 0)
    {
        close(socketO);
    }

    return ret;
}
