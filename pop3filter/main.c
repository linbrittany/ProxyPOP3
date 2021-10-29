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
static address_info proxy_address;
static address_info admin_proxy_address;

static int proxy = -1;
static int admin_proxy = -1;

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
    parse_args(argc, argv, &args, &proxy_address);

    set_address(&proxy_address, args.listen_pop3_address);
    set_address(&admin_proxy_address, args.listen_pop3_admin_address);

    const char *err_msg = NULL;
    selector_status ss = SELECTOR_SUCCESS;
    fd_selector selector = NULL;

    if ((proxy = socket(proxy_address.domain, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        printf("Unable to create socket for proxy");
        exit(1);
    }

    if ((admin_proxy = socket(proxy_address.domain, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        printf("Unable to create socket for proxy admin");
        exit(1);
    }

    int ans = -1;

    if ((ans = bind(proxy, (struct sockaddr *)&admin_proxy_address.addr.storage, sizeof(admin_proxy_address.addr.storage))) < 0)
    {
        err_msg = "bind() failed for proxy";
        close(proxy);
        exit(1);
    }

    if ((ans = bind(admin_proxy, (struct sockaddr *)&admin_proxy_address.addr.storage, sizeof(admin_proxy_address.addr.storage))) < 0)
    {
        err_msg = "bind() failed for proxy admin";
        close(admin_proxy);
        exit(1);
    }

    if ((ans = listen(proxy, BACKLOG)) < 0)
    {
        err_msg = "listen() failed for proxy";
        close(proxy);
        exit(1);
    }

    if ((ans = listen(admin_proxy, BACKLOG)) < 0)
    {
        err_msg = "listen() failed for proxy admin";
        close(admin_proxy);
        exit(1);
    }

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);

    if (selector_fd_set_nio(proxy) == -1)
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

    if ((ss = selector_register(selector, proxy, &pop3, OP_READ, NULL)) != SELECTOR_SUCCESS)
    {
        err_msg = "registering fd for proxy";
        goto finally;
    }

    if ((ss = selector_register(selector, admin_proxy, &pop3_admin, OP_READ, NULL)) != SELECTOR_SUCCESS)
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

    if (proxy >= 0)
    {
        close(proxy);
    }
    return ret;
}
