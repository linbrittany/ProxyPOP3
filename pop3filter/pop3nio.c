#include <stdio.h>
#include <stdlib.h>  // malloc
#include <string.h>  // memset
#include <assert.h>  // assert
#include <errno.h>
#include <time.h>
#include <unistd.h>  // close
#include <pthread.h>
#include <sys/socket.h>

#include "./include/selector.h"
#include "./include/stm.h"

static const struct fd_handler pop3_handler = {
    // .handle_read = socksv5_read,
    // .handle_write = socksv5_write,
    // .handle_close = socksv5_close,
    // .handle_block = socksv5_block,
};

struct pop3 {
    // /** maquinas de estados */
    // struct state_machine stm;

    // /** estados para el client_fd */
    // union {
    //     struct hello_st hello;
    //     struct request_st request;
    //     struct copy copy;
    // } client;
    // /** estados para el origin_fd */
    // union {
    //     struct connecting conn;
    //     struct copy copy;
    // } orig;
};

void
pop3_passive_accept(struct selector_key *key) {
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    struct pop3 *state = NULL;

    const int client = accept(key->fd, (struct sockaddr*) &client_addr, &client_addr_len);
    if(client == -1) {
        goto fail;
    }
    if(selector_fd_set_nio(client) == -1) {
        goto fail;
    }
    state = socks5_new(client);
    if(state == NULL) {
        // sin un estado, nos es imposible manejaro.
        // tal vez deberiamos apagar accept() hasta que detectemos
        // que se liberÃ³ alguna conexiÃ³n.
        goto fail;
    }
    // memcpy(&state->client_addr, &client_addr, client_addr_len);
    // state->client_addr_len = client_addr_len;

    if(SELECTOR_SUCCESS != selector_register(key->s, client, &pop3_handler, OP_READ, state)) {
        goto fail;
    }
    return ;

fail:
    if(client != -1) {
        close(client);
    }
    socks5_destroy(state);
}

