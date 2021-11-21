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
};

struct proxy_metrics {
    unsigned long active_connections;
    unsigned long total_connections;

    unsigned long bytes_transferred;
};

struct proxy_args args;
struct proxy_metrics metrics;

void pop3_passive_accept(struct selector_key *key);
void pop3_pool_destroy();

#endif
