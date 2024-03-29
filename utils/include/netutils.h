#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <netinet/in.h>
#include "buffer.h"

typedef enum address_type {
    ADDR_IPV4   = 0x01,
    ADDR_IPV6   = 0x02,
    ADDR_DOMAIN = 0x03,
} address_type;

typedef union address {
    struct sockaddr_storage storage;
    char fqdn[0xFF];
} address;

typedef struct address_info {
    address_type type;
    address addr;
    socklen_t addr_len;
    int domain;
    in_port_t port;
} address_info;

address_info proxy_addr;
address_info admin_proxy_addr;

void set_address(address_info * address, const char * ip);
#endif
