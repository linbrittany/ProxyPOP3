#include <stdio.h>
#include <stdlib.h>  // malloc
#include <string.h>  // memset
#include <assert.h>  // assert
#include <errno.h>
#include <time.h>
#include <unistd.h>  // close
#include <pthread.h>
#include <netdb.h>
#include <sys/socket.h>

#include "selector.h"
#include "stm.h"
#include "pop3nio.h"
#include "buffer.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

/** maquina de estados general */
enum pop3state {
    RESOLVING,
    CONNECTING,
    HELLO,
    CHECK_CAPABILITIES,
    COPY,
    SEND_ERROR_MSG,

    // estados terminales
    DONE,
    ERROR,
};

/** obtiene el struct (pop3 *) desde la llave de seleccion  */
#define ATTACHMENT(key) ( (struct pop3 *)(key)->data)

/* declaracion forward de los handlers de seleccion de una conexion
 * establecida entre un cliente y el proxy.
 */
static void pop3_read   (struct selector_key *key);
static void pop3_write  (struct selector_key *key);
static void pop3_block  (struct selector_key *key);
static void pop3_close  (struct selector_key *key);

static const struct fd_handler pop3_handler = {
    .handle_read   = pop3_read,
    .handle_write  = pop3_write,
    .handle_close  = pop3_close,
    .handle_block  = pop3_block,
};

//Funciones para los distintos estados del cliente
static const struct state_definition client_state_def[] = {
    {
        .state = RESOLVING,
        .on_block_ready = NULL, 
    }, {
        .state = CONNECTING,
    }, {
        .state = HELLO,
    }, {
        .state = CHECK_CAPABILITIES,
    }, {
        .state = COPY,
    }, {
        .state = SEND_ERROR_MSG,
    }
};

struct pop3 {
    int client_fd;
    int origin_fd;

    /** maquinas de estados */
    struct state_machine stm;

    struct buffer * read_buffer;
    struct buffer * write_buffer;

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

    struct addrinfo *origin_resolution; //No pisarlo porque hay que liberarlo
    struct addrinfo *curr_origin_resolution; 

    unsigned references;

    struct pop3 * next;
};

 /** realmente destruye */
static void pop3_destroy_(struct pop3* s) {
    if(s->origin_resolution != NULL) {
        freeaddrinfo(s->origin_resolution);
        s->origin_resolution = 0;
    }
    free(s);
}

/**
 * destruye un  `struct pop3', tiene en cuenta las referencias
 * y el pool de objetos.
 */
static void pop3_destroy(struct pop3 *s) {
    if(s == NULL) {
        // nada para hacer
    } else if(s->references == 1) {
        if(s != NULL) {
//             if(pool_size < max_pool) {
//                 s->next = pool;
//                 pool    = s;
//                 pool_size++;
//             } else {
//                 pop3_destroy_(s);
//             }
            pop3_destroy_(s);
        }
    } else {
        s->references -= 1;
    }
}

static struct pop3 * pop3_new(int client_fd, size_t buffer_size) {
    struct pop3 * new_pop3 = malloc(sizeof(struct pop3));
    memset(new_pop3, 0, sizeof(struct pop3));
    new_pop3->client_fd = client_fd;
    new_pop3->origin_fd = -1;
    new_pop3->read_buffer = buffer_init(buffer_size);

    new_pop3->stm.initial = RESOLVING;
    new_pop3->stm.max_state = ERROR;
    new_pop3->stm.states = client_state_def;
    stm_init(&new_pop3->stm);
    return new_pop3;
}

void pop3_passive_accept(struct selector_key *key) {
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
    state = pop3_new(client, args.buffer_size);
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

    //Crear socket entre proxy y servidor origen y registrarlo para escritura

fail:
    if(client != -1) {
        close(client);
    }
    pop3_destroy(state);
}

// Handlers top level de la conexion pasiva.
// son los que emiten los eventos a la maquina de estados.
static void pop3_done(struct selector_key* key);

static void pop3_read(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum pop3state st = stm_handler_read(stm, key);

    if(ERROR == st || DONE == st) {
        pop3_done(key);
    }
}

static void pop3_write(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum pop3state st = stm_handler_write(stm, key);

    if(ERROR == st || DONE == st) {
        pop3_done(key);
    }
}

static void pop3_block(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    const enum pop3state st = stm_handler_block(stm, key);

    if(ERROR == st || DONE == st) {
        pop3_done(key);
    }
}

static void pop3_close(struct selector_key *key) {
    pop3_destroy(ATTACHMENT(key));
}

static void pop3_done(struct selector_key *key) {
    const int fds[] = {
        ATTACHMENT(key)->client_fd,
        ATTACHMENT(key)->origin_fd,
    };
    for(unsigned i = 0; i < N(fds); i++) {
        if(fds[i] != -1) {
            if(SELECTOR_SUCCESS != selector_unregister_fd(key->s, fds[i])) {
                abort();
            }
            close(fds[i]);
        }
    }
}

