#ifndef POP3_NIO_H
#define POP3_NIO_H

#include <buffer.h>
#include "selector.h"


struct proxy_args {
    size_t buffer_size;
    char *stderr_file_path;
    char *listen_pop3_admin_address;
    char *listen_pop3_address;
    char *listen_origin_address;
    char *command;
    char *admin_credential;
};

struct proxy_metrics {
    unsigned long long active_connections;
    unsigned long long total_connections;

    unsigned long long bytes_transferred;
};

struct copy
{
    int *fd;
    buffer *read_b, *write_b;
    struct copy *other;
    fd_interest duplex;
};

struct proxy_args args;
struct proxy_metrics metrics;

extern char * addressOrigin;

void pop3_passive_accept(struct selector_key *key);
void pop3_pool_destroy();

#endif
