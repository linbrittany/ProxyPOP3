#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>

#include "./include/args.h"
#include "../utils/include/netutils.h"
#include "./include/selector.h"

#define BACKLOG 250

static struct proxyArgs args;
static addressInfo proxyAddress;

static int proxy = -1;

static bool done = false;

static void sigterm_handler(const int signal) {
    printf("signal %d, cleaning up and exiting\n",signal);
    done = true;
}

static void setUpProxyArgs(void) {
    args.proxyAddr = "0.0.0.0";
}

int main(int argc, char const **argv) {
    memset(&args, 0, sizeof(args));
    setUpProxyArgs();
    parseArgs(argc, argv, &args, &proxyAddress);

    setAddress(&proxyAddress, args.proxyAddr);

    const char *err_msg = NULL;
    selector_status ss = SELECTOR_SUCCESS;
    fd_selector selector = NULL;
    
    if ((proxy = socket(proxyAddress.domain, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        printf("Unable to create socket for proxy");
        exit(1);
    }

    int ans = bind(proxy, (struct sockaddr*) &proxyAddress.addr.storage, sizeof(proxyAddress.addr.storage));
    
    if (ans < 0) {
        err_msg = "bind() failed for proxy";
        close(proxy);
        exit(1);
    }

    if ((ans = listen(proxy, BACKLOG)) < 0) {
        err_msg = "listen() failed for proxy";
        close(proxy);
        exit(1);
    }

    signal(SIGTERM, sigterm_handler);
    signal(SIGINT,  sigterm_handler);

    if(selector_fd_set_nio(proxy) == -1) {
        err_msg = "getting server proxy flags";
        goto finally;
    }
    const struct selector_init conf = {
        .signal = SIGALRM,
        .select_timeout = {
            .tv_sec  = 10,
            .tv_nsec = 0,
        },
    };

    selector = selector_new(1024);
    if(selector == NULL) {
        err_msg = "unable to create selector";
        goto finally;
    }
    const struct fd_handler pop3 = {
        .handle_read       = NULL,
        .handle_write      = NULL,
        .handle_close      = NULL, // nada que liberar
    };

    ss = selector_register(selector, proxy, &pop3, OP_READ, NULL);

    if(ss != SELECTOR_SUCCESS) {
        err_msg = "registering fd";
        goto finally;
    }
    for(;!done;) {
        err_msg = NULL;
        ss = selector_select(selector);
        if(ss != SELECTOR_SUCCESS) {
            err_msg = "serving";
            goto finally;
        }
    }
    if(err_msg == NULL) {
        err_msg = "closing";
    }

    int ret = 0;

    finally:
    if(ss != SELECTOR_SUCCESS) {
        fprintf(stderr, "%s: %s\n", (err_msg == NULL) ? "": err_msg, ss == SELECTOR_IO ? strerror(errno) : selector_error(ss));
        ret = 2;
    } else if(err_msg) {
        perror(err_msg);
        ret = 1;
    }
    if(selector != NULL) {
        selector_destroy(selector);
    }
    selector_close();

    socksv5_pool_destroy();

    if(proxy >= 0) {
        close(proxy);
    }
    return ret;
}
