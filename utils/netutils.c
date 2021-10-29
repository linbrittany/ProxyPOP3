#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

#include "include/netutils.h"

void set_address(address_info * address, const char * ip) {
    memset(&(address->addr.storage), 0, sizeof(address->addr.storage));
    address->type = ADDR_IPV4;
    address->domain = AF_INET;
    address->addr_length = sizeof(struct sockaddr_in);
    struct sockaddr_in origin_ip4;
    memset(&(origin_ip4), 0, sizeof(origin_ip4));
    origin_ip4.sin_family = AF_INET;
    int ans = 0;
    if ((ans = inet_pton(AF_INET, ip, &origin_ip4.sin_addr.s_addr)) <= 0) {
        address->type = ADDR_IPV6;
        address->domain = AF_INET6;
        address->addr_length = sizeof(struct sockaddr_in6);
        struct sockaddr_in6 origin_ip6;
        memset(&(origin_ip6), 0, sizeof(origin_ip6));
        origin_ip6.sin6_family = AF_INET6;
        if ((ans = inet_pton(AF_INET6, ip, &origin_ip6.sin6_addr.s6_addr)) <= 0) {
            address->type = ADDR_DOMAIN;
            memcpy(address->addr.fqdn, ip, strlen(ip));
            return;
        }
        origin_ip6.sin6_port = htons(address->port);
        memcpy(&address->addr.storage, &origin_ip6, sizeof(address->addr_length));
        return;
    }
    origin_ip4.sin_port = htons(address->port);
    memcpy(&address->addr.storage, &origin_ip4, sizeof(address->addr_length));
    return;
}
