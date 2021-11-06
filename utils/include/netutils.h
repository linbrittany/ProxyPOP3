#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <netinet/in.h>

typedef enum address_type {
    ADDR_IPV4   = 0x01,
    ADDR_IPV6   = 0x02,
    ADDR_DOMAIN = 0x03,
} address_type;

typedef union address {
    struct sockaddr_storage storage;
    char fqdn[0xFF];
} address;


int set_address( int port, const char * ip);
#endif
