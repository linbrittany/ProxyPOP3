#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "adminnio.h"
#include "logger.h"

#define BUFFER_MAX_SIZE 1024

void admin_passive_accept(struct selector_key *key) {
    char buffer[BUFFER_MAX_SIZE] = {0};
    unsigned int len, n;
    struct sockaddr_in6 clntAddr;

    n = recvfrom(key->fd, buffer, BUFFER_MAX_SIZE, 0, (struct sockaddr *) &clntAddr, &len);
    if ( buffer[n-1] == '\n') // Por si lo estan probando con netcat, en modo interactivo
		n--;
	buffer[n] = '\0';
	log(DEBUG, "UDP received:%s", buffer );

    

    return;
}
