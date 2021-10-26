#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

#include "./include/netutils.h"

void setAddress(addressInfo * address, const char * ip) {
    memset(&(address->addr.storage), 0, sizeof(address->addr.storage));
    address->type = ADDR_IPV4;
    address->domain = AF_INET;
    address->addrLength = sizeof(struct sockaddr_in);
    struct sockaddr_in originIpv4;
    memset(&(originIpv4), 0, sizeof(originIpv4));
    originIpv4.sin_family = AF_INET;
    int ans = 0;
    if ((ans = inet_pton(AF_INET, ip, &originIpv4.sin_addr.s_addr)) <= 0) {
        address->type = ADDR_IPV6;
        address->domain = AF_INET6;
        address->addrLength = sizeof(struct sockaddr_in6);
        struct sockaddr_in6 originIpv6;
        memset(&(originIpv6), 0, sizeof(originIpv6));
        originIpv6.sin6_family = AF_INET6;
        if ((ans = inet_pton(AF_INET6, ip, &originIpv6.sin6_addr.s6_addr)) <= 0) {
            address->type = ADDR_DOMAIN;
            memcpy(address->addr.fqdn, ip, strlen(ip));
            return;
        }
        originIpv6.sin6_port = htons(address->port);
        memcpy(&address->addr.storage, &originIpv6, sizeof(address->addrLength));
        return;
    }
    originIpv4.sin_port = htons(address->port);
    memcpy(&address->addr.storage, &originIpv4, sizeof(address->addrLength));
    return;
}