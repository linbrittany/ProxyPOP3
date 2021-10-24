#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "args.h"

struct proxyArgs args;

static void setUpProxyArgs(void) {
    args.proxyAddr = "0.0.0.0";
    args.proxyPort = 1080;
}

int main(int argc, char const *argv[])
{
    setUpProxyArgs();
    parseArgs(argc, argv, &args);
    return 0;
}
