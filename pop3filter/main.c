#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "./include/args.h"
#include "../utils/include/netutils.h"

#define BACKLOG 250

static struct proxyArgs args;
static addressInfo proxyAddress;

static int proxy = -1;

static void setUpProxyArgs(void) {
    args.proxyAddr = "0.0.0.0";
}

int main(int argc, char const **argv) {
    memset(&args, 0, sizeof(args));
    setUpProxyArgs();
    parseArgs(argc, argv, &args, &proxyAddress);

    setAddress(&proxyAddress, args.proxyAddr);
    
    if ((proxy = socket(proxyAddress.domain, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        printf("Unable to create socket for proxy");
        exit(1);
    }

    int ans = bind(proxy, (struct sockaddr*) &proxyAddress.addr.storage, sizeof(proxyAddress.addr.storage));
    
    if (ans < 0) {
        printf("bind() failed for proxy");
        close(proxy);
        exit(1);
    }

    if ((ans = listen(proxy, BACKLOG)) < 0) {
        printf("listen() failed for proxy");
        close(proxy);
        exit(1);
    }

    printf("listening on TCP port %d", proxyAddress.port);

    

    return 0;
}
