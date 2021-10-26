#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <netinet/in.h>

typedef enum addressType {
    ADDR_IPV4   = 0x01,
    ADDR_IPV6   = 0x02,
    ADDR_DOMAIN = 0x03,
} addressType;

typedef union address {
    struct sockaddr_storage storage;
    char fqdn[0xFF];
} address;

typedef struct addressInfo {
    addressType type;
    in_port_t port;
    int domain;
    address addr;
    struct in_addr addr2;
    socklen_t addrLength;
} addressInfo;

void setAddress(addressInfo * address, const char * ip);

#endif