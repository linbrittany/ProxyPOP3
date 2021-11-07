#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>

#include "include/netutils.h"

void set_address(address_info * address, const char * ip) {
   
    // int socket_r;
    // int opt = true;

   	// if ((socket_r = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == 0)
	// {
	// 	socket_r = 0;
    //     return -1; // error
	// }

    // if (setsockopt(socket_r, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
	// {
	// 		return -1; //error
	// }

    // struct sockaddr_in address;
    // memset(&address, 0, sizeof(address));
	// address.sin_port = htons(port);
	// address.sin_addr.s_addr = INADDR_ANY;
	// address.sin_family = AF_INET;
    memset(&address->addr.storage, 0, sizeof(address->addr.storage));
    struct sockaddr_in origin_ip4; 
    memset(&origin_ip4, 0, sizeof(origin_ip4));
    origin_ip4.sin_family = AF_INET;
    origin_ip4.sin_port = htons(address->port);
    int ans = 0;
    if ((ans = inet_pton(AF_INET, ip, &origin_ip4.sin_addr.s_addr)) <= 0) {
        struct sockaddr_in6 origin_ip6;
		memset(&origin_ip6, 0, sizeof(origin_ip6));
		origin_ip6.sin6_family = AF_INET6;
		origin_ip6.sin6_port = htons(address->port);
		// address.sin6_addr = in6addr_any;
        // address.sin6_family = AF_INET6;

        if ((ans = inet_pton(AF_INET6, ip, &origin_ip6.sin6_addr.s6_addr)) <= 0) {
            //address.type = ADDR_DOMAIN;
            //memcpy(address.addr.fqdn, ip, strlen(ip));
         //revisar
            return; //??
        }

        //    if (bind(socket_r, (struct sockaddr *)&address, sizeof(address)) < 0)
		// 	{
				
		// 		close(socket_r);
		// 		return -1;//error
		// 	}
        address->type = ADDR_IPV6;
        address->domain = AF_INET6;
        address->addr_len = sizeof(struct sockaddr_in6);
        memcpy(&address->addr.storage, &origin_ip6, address->addr_len);
        return;
    }
    // if (bind(socket_r, (struct sockaddr *)&address, sizeof(address)) < 0)
	// {
                
	// 			close(socket_r);
	// 			return -1;//error
	// }

    address->type = ADDR_IPV4;
    address->domain = AF_INET;
    address->addr_len = sizeof(struct sockaddr_in);
    memcpy(&address->addr.storage, &origin_ip4, address->addr_len);
    return;
}
