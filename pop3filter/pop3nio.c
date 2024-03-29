#include <stdio.h>
#include <stdlib.h>  // malloc
#include <string.h>  // memset
#include <assert.h>  // assert
#include <errno.h>
#include <time.h>
#include <unistd.h>  // close, read?
#include <pthread.h>
#include <netdb.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdint.h>

// #include "selector.h"
#include "stm.h"
#include "pop3nio.h"
#include "buffer.h"
#include "netutils.h"
#include "parser_multiline.h"

/*parsers*/
#include "hello_parser.h"
#include "capa_parser.h"
#include "command_parser.h"
#include "response_parser.h"

/*utils*/
#include "../utils/include/queue.h"
#include "../utils/include/buffer.h"
#include "../utils/include/logger.h"
#include "../utils/include/netutils.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))
#define MAX_BUFF 4000

char * addressOrigin;


//In[W] -----> pipe 1 IN[R]
//Out[R]<------ pipe 2 Out[W]

   enum{
        R=0,
        W=1
    };


struct filter{
    int                 in[2];
    int                 out[2];
    pid_t               pid; // pid del proceso creado ej cat
    //char * command;
    //fd_interest duplex[2]; // los intereses
};



struct check_capa
{
    buffer       *read_b;
    capabilities *capabilities;
    capa_parser   parser;
};


typedef struct hello_st {
    struct buffer * write_buffer;
    struct hello_parser parser;
} hello_st;

typedef struct error_container {
    char * message;
    size_t message_length;
    size_t sended_size;
} error_container;


struct pop3 {
    int client_fd;
    int origin_fd;

    int flag_filter;

    /** maquinas de estados */
    struct state_machine stm;

    struct buffer * read_buffer;
    struct buffer * write_buffer;

    struct buffer * headers;

    /* Persisto si origen soporta una lista de capabilities de POP3 */
    capabilities origin_capabilities;

    struct Queue* commands_queue;

    error_container error_sender;

    struct filter filter_data;

    /** estados para el client_fd */
    union {
        //struct request_st request;
        struct copy copy;
     } client;
    /** estados para el origin_fd */
     union {
        struct hello_st hello;
        //struct connecting conn;
        struct copy copy;

        struct check_capa capabilities;

        
     } origin;
     union{

        struct copy copy;
        int filter_state_in;
     }filter_in;
      union{
        int filter_state_out;
        struct copy copy;

     }filter_out;
   
   
    address_info origin_addr_data;

    struct addrinfo *origin_resolution; //No pisarlo porque hay que liberarlo
    struct addrinfo *curr_origin_resolution;

    struct cmd_parser command_parser;
    struct rsp_parser response_parser;

    unsigned references;

    struct pop3 * next;
};


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

static void *resolv_blocking(void * data );
static unsigned resolv_done(struct selector_key* key);
static unsigned write_error_msg(struct selector_key * key);
static unsigned connecting(fd_selector s, struct pop3 * proxy);
static unsigned connection_done(struct selector_key * key);
static void hello_init(const unsigned state, struct selector_key * key);
static unsigned hello_read(struct selector_key * key);
static unsigned hello_write(struct selector_key * key);
static unsigned copy_w(struct selector_key *key);
static unsigned copy_r(struct selector_key *key);
static fd_interest copy_interest(fd_selector s, struct copy *c);
static void copy_init(const unsigned state, struct selector_key *key);
static void check_capa_init(const unsigned state, struct selector_key *key);
static unsigned check_capa_read(struct selector_key *key);
static unsigned check_capa_write(struct selector_key *key);
struct copy * copy_ptr(struct selector_key * key) ;


static void filter_init(struct selector_key *key);
static void filter_write(struct selector_key *key);
static void filter_read(struct selector_key *key);
static void filter_block(struct selector_key *key);
static void filter_close(struct selector_key *key);
static void want_filter(bool filter, struct selector_key * key);

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
        .on_block_ready = resolv_done,
    }, {
        .state = CONNECTING,
        .on_write_ready = connection_done,
    }, {
        .state = HELLO,
        .on_arrival = hello_init,
        .on_read_ready = hello_read,
        .on_write_ready = hello_write,
    }, {
        .state = CHECK_CAPABILITIES,
        .on_arrival = check_capa_init,
        .on_read_ready = check_capa_read,
        .on_write_ready = check_capa_write,

    }, {
        .state = COPY,
        .on_arrival = copy_init,
        .on_read_ready = copy_r,
        .on_write_ready = copy_w,
    }, {
        .state = SEND_ERROR_MSG,
        .on_write_ready = write_error_msg,
    }, {
        .state = DONE,
    }, {
        .state = ERROR,
    }
};

static const unsigned pool_max_size = 50;
static unsigned pool_current_size = 0;
static struct pop3 * current_pool = 0;

 /** realmente destruye */
static void pop3_destroy_(struct pop3* s) {
    buffer_delete(s->read_buffer);
    buffer_delete(s->write_buffer);
    buffer_delete(ATTACHMENT(key)->headers);

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
    if (s == NULL) {
        // nada para hacer
    } else if (s->references == 1) {
        if (pool_current_size == pool_max_size) {
            pop3_destroy_(s);
        } else {
            // s->next = current_pool;
            current_pool = s;
            pool_current_size++;
        }
    } else {
        s->references -= 1;
    }
}

void pop3_pool_destroy() {
    struct pop3 * next, * current;
    for (current = current_pool; current != NULL; current = next) {
        next = current->next;
        pop3_destroy_(current);
    }
}

static struct pop3 * pop3_new(int client_fd, size_t buffer_size, address_info origin_addr_data) {

    struct pop3 * to_ret;
    buffer * read_buff;
    buffer * write_buff;


    if (current_pool == NULL) {
        to_ret = malloc(sizeof(struct pop3));
        read_buff = buffer_init(buffer_size);
        write_buff = buffer_init(buffer_size);
    } else {
        to_ret = current_pool;
        current_pool = current_pool->next;
        to_ret->next = 0;
        read_buff = to_ret->read_buffer;
        write_buff = to_ret->write_buffer;
        buffer_reset(read_buff);
        buffer_reset(write_buff);
    }

    to_ret->flag_filter = 0;
    
    memset(to_ret, 0, sizeof(sizeof(struct pop3)));

    to_ret->client_fd = client_fd;
    to_ret->origin_fd = -1;
    to_ret->read_buffer = read_buff;
    to_ret->write_buffer = write_buff;
    to_ret->origin_addr_data = origin_addr_data;
    to_ret->commands_queue = create_queue();

    to_ret->stm.initial = RESOLVING;
    to_ret->stm.max_state = ERROR;
    to_ret->stm.states = client_state_def;
    stm_init(&to_ret->stm);

    cmd_parser_init(&to_ret->command_parser);
    rsp_parser_init(&to_ret->response_parser);

    return to_ret;
}

void pop3_passive_accept(struct selector_key *key) {
 
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    address_info * origin_addr_data = (address_info *) key->data;
    struct pop3 *state = NULL;
    pthread_t tid;

    metrics.active_connections++;
    metrics.total_connections++;

    const int client = accept(key->fd, (struct sockaddr*) &client_addr, &client_addr_len);
    if(client == -1) {
        goto fail;
    }
    if(selector_fd_set_nio(client) == -1) {
        goto fail;
    }
    state = pop3_new(client, args.buffer_size, *origin_addr_data);
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

    if(origin_addr_data->type != ADDR_DOMAIN) 
        state->stm.initial = connecting(key->s, state);
    else {
        // logInfo("Need to resolv the domain name: %s.", origin_addr_data->addr.fqdn);
        struct selector_key * blockingKey = malloc(sizeof(*blockingKey));
        if(blockingKey == NULL)
            goto fail2;

        blockingKey->s  = key->s;
        blockingKey->fd   = client;
        blockingKey->data = state;
        if(-1 == pthread_create(&tid, 0, resolv_blocking, blockingKey)) {
            // logError("Unable to create a new thread. Client Address: %s", state->session.clientString);
            // proxy->error_sender.message = "-ERR Unable to connect.\r\n";
            if(SELECTOR_SUCCESS != selector_set_interest(key->s, state->client_fd, OP_WRITE))
                goto fail2;
            state->stm.initial = SEND_ERROR_MSG;
        }
    }

    return;
fail2:
    selector_unregister_fd(key->s, client);
fail:
    metrics.active_connections--;
    if(client != -1) {
        close(client);
    }
    pop3_destroy(state);
}

/**
 * Realiza la resolución de DNS bloqueante.
 *
 * Una vez resuelto notifica al selector para que el evento esté
 * disponible en la próxima iteración.
 */
static void *resolv_blocking(void * data ) {
    struct selector_key* key = (struct selector_key * ) data;
    struct pop3 *proxy = ATTACHMENT(key);

    pthread_detach(pthread_self());
    proxy->origin_resolution = 0;
    struct addrinfo hints = {
        .ai_family    = AF_UNSPEC,    
        /** Permite IPv4 o IPv6. */
        .ai_socktype  = SOCK_STREAM,  
        .ai_flags     = AI_PASSIVE,   
        .ai_protocol  = 0,        
        .ai_canonname = NULL,
        .ai_addr      = NULL,
        .ai_next      = NULL,
    };

    char buff[7];
    snprintf(buff, sizeof(buff),"%d", proxy->origin_addr_data.port);
    getaddrinfo(proxy->origin_addr_data.addr.fqdn,buff,&hints,&proxy->origin_resolution);
    selector_notify_block(key->s,key->fd);

    free(data);
    return 0;
}

// Handlers top level de la conexion pasiva.
// son los que emiten los eventos a la maquina de estados.
static void pop3_done(struct selector_key* key);

static void pop3_read(struct selector_key *key) {
    struct state_machine *stm   = &ATTACHMENT(key)->stm;
    log(DEBUG, "FILE DESC %d\n",key->fd);
    const enum pop3state st = stm_handler_read(stm, key);

    if(ERROR == st || DONE == st) {
        pop3_done(key);
    }
}

static void pop3_write(struct selector_key *key) {
    struct state_machine *stm = &ATTACHMENT(key)->stm;
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

//RESOLVING

/**
 * Procesa el resultado de la resolución de nombres. 
 */
static unsigned resolv_done(struct selector_key* key) {
    
    struct pop3 * proxy = ATTACHMENT(key);
    if(proxy->origin_resolution != 0) {
        proxy->origin_addr_data.domain = proxy->origin_resolution->ai_family;
        proxy->origin_addr_data.addr_len = proxy->origin_resolution->ai_addrlen;
        memcpy(&proxy->origin_addr_data.addr.storage,
                proxy->origin_resolution->ai_addr,
                proxy->origin_resolution->ai_addrlen);
        freeaddrinfo(proxy->origin_resolution);
        proxy->origin_resolution = 0;
    } else {
        // proxy->error_sender.message = "-ERR Connection refused.\r\n";
        if(SELECTOR_SUCCESS != selector_set_interest(key->s, proxy->client_fd, OP_WRITE))
            return ERROR;
        return SEND_ERROR_MSG;
    }

    return connecting(key->s, proxy);
}

/** 
 * Intenta establecer una conexión con el origin server. 
 */
static unsigned connecting(fd_selector s, struct pop3 * proxy) {
   
    address_info originAddrData = proxy->origin_addr_data;

    proxy->origin_fd = socket(originAddrData.domain, SOCK_STREAM, IPPROTO_TCP);

    if(proxy->origin_fd == -1)
        goto finally;
    if(selector_fd_set_nio(proxy->origin_fd) == -1)
        goto finally;

    if(connect(proxy->origin_fd, (const struct sockaddr *) &originAddrData.addr.storage, originAddrData.addr_len) == -1) {
        if(errno == EINPROGRESS) {
            /**
             * Es esperable,  tenemos que esperar a la conexión.
             * Dejamos de pollear el socket del cliente.
             */
            selector_status status = selector_set_interest(s, proxy->client_fd, OP_NOOP);
            if(status != SELECTOR_SUCCESS) 
                goto finally;

            /** Esperamos la conexion en el nuevo socket. */
            status = selector_register(s, proxy->origin_fd, &pop3_handler, OP_WRITE, proxy);
            log(INFO, "Registering origin: %d\n", proxy->origin_fd);
            if(status != SELECTOR_SUCCESS) 
                goto finally;
        
            proxy->references += 1;
        }
    } else {
        /**
         * Estamos conectados sin esperar... no parece posible
         * Saltaríamos directamente a COPY.
         */
        // logError("Problem: connected to origin server without wait. Client Address: %s", proxy->session.clientString);
    }

    return CONNECTING;

finally:    
    // logError("Problem connecting to origin server. Client Address: %s", proxy->session.clientString);
    proxy->error_sender.message = "-ERR Connection refused.\r\n";
    if(SELECTOR_SUCCESS != selector_set_interest(s, proxy->client_fd, OP_WRITE))
        return ERROR;
    return SEND_ERROR_MSG;
}

//CONNECTING

static unsigned connection_done(struct selector_key * key) {

    struct pop3 * proxy_pop3 = ATTACHMENT(key);
    int error = -1;
    socklen_t len = sizeof(error);

    if ((error = getsockopt(key->fd, SOL_SOCKET, SO_ERROR, &error, &len)) < 0) {

        if (SELECTOR_SUCCESS == selector_set_interest(key->s, proxy_pop3->client_fd, OP_WRITE)) { //Setear cliente para escritura
            log(INFO, "Setting fd %d to be written\n", proxy_pop3->client_fd);
            return SEND_ERROR_MSG;
        }
        return ERROR;
    }
    else  if (SELECTOR_SUCCESS == selector_set_interest_key(key, OP_READ)) { //Setear origin para leer
        log(INFO, "Connection established for client %d and origin %d\n", proxy_pop3->client_fd, key->fd);
        return HELLO;
    }
    return ERROR;
}

//HELLO

static void hello_init(const unsigned state, struct selector_key * key) {
    struct pop3 * proxy = ATTACHMENT(key);
    struct hello_st * hello = &proxy->origin.hello;
    hello_parser_init(&hello->parser);
    hello->write_buffer = proxy->write_buffer;
}

static unsigned hello_read(struct selector_key * key) {
    struct pop3 * proxy = ATTACHMENT(key);
    struct hello_st * hello = &proxy->origin.hello;
    struct buffer * buff = hello->write_buffer;
    bool error = false;
    size_t len;
    uint8_t * write_ptr = buffer_write_ptr(buff, &len);
    ssize_t n = recv(key->fd, write_ptr, len, 0);
    log(INFO, "Receiving %zd bytes from fd %d\n", n, key->fd);
    if (n > 0) {
        buffer_write_adv(buff, n);
        hello_consume(buff, &hello->parser, &error);
        if (!error && SELECTOR_SUCCESS == selector_set_interest(key->s, proxy->origin_fd, OP_NOOP) &&
                SELECTOR_SUCCESS == selector_set_interest(key->s, proxy->client_fd, OP_WRITE)) {
            return HELLO;
        }
        else {
            error = true;
        }
    }
    else {
        shutdown(key->fd, SHUT_RD);
        error = true;
    }
    if (error) {
        proxy->error_sender.message = "-ERR\r\n";
        if (SELECTOR_SUCCESS == selector_set_interest(key->s, proxy->client_fd, OP_WRITE)) {
            return SEND_ERROR_MSG;
        }
        return ERROR;
    }
    return HELLO;
}

static unsigned hello_write(struct selector_key * key) {
    struct pop3 * proxy = ATTACHMENT(key);
    struct hello_st * hello = &proxy->origin.hello;
    struct buffer * buff = hello->write_buffer;
    size_t len;
    uint8_t * read_ptr = buffer_read_ptr(buff, &len);
    ssize_t n = send(key->fd, read_ptr, len, 0);
    if (n == -1) {
        shutdown(key->fd, SHUT_WR);
        return ERROR;
    }
    else {
        if (hello_is_done(hello->parser.state, 0)) {
            if(SELECTOR_SUCCESS == selector_set_interest(key->s, proxy->origin_fd, OP_WRITE) &&
               SELECTOR_SUCCESS == selector_set_interest_key(key, OP_NOOP)){
                buffer_read_adv(buff, n);
                metrics.bytes_transferred += n;
                return CHECK_CAPABILITIES;
            } 
            else
                return ERROR;
        }
        if (!buffer_can_read(buff) && SELECTOR_SUCCESS == selector_set_interest(key->s, proxy->origin_fd, OP_READ) &&
                SELECTOR_SUCCESS == selector_set_interest(key->s, proxy->client_fd, OP_NOOP)) {
            return HELLO;
        }
    }
    return ERROR;
}

//CAPA

static void check_capa_init(const unsigned state, struct selector_key *key){
    struct pop3 * proxy = ATTACHMENT(key);
    struct check_capa * check_capabilities = &proxy->origin.capabilities;
    check_capabilities->capabilities = &proxy->origin_capabilities;
    check_capabilities->read_b = proxy->read_buffer;
    capa_parser_init(&check_capabilities->parser, check_capabilities->capabilities);
}

static const char *capa_msg = "CAPA\n";
static const int CAPA_MSG_LEN = 5;


/*lectura y parseo de la respuesta del origen */
static unsigned check_capa_read(struct selector_key *key){
    struct pop3 * proxy = ATTACHMENT(key);
    struct check_capa * check_capabilities = &proxy->origin.capabilities;
    buffer * buff = check_capabilities->read_b;
    bool error = false;
    size_t len;
    uint8_t * write_ptr = buffer_write_ptr(buff, &len);
    ssize_t n = recv(key->fd, write_ptr, len, 0);
    if( n > 0){
        buffer_write_adv(buff,n);
        capa_state parser_state  = capa_parser_consume(&check_capabilities->parser, buff, &error);
        if(error){
            log(ERR, "Error en el parser del comando CAPA %d\n",1);
            return ERROR;
        }
        if(capa_parser_done(parser_state,0)) {
            if(SELECTOR_SUCCESS == selector_set_interest(key->s, proxy->client_fd, OP_READ) &&
               SELECTOR_SUCCESS == selector_set_interest_key(key, OP_READ)){
                log(INFO, "Origin %s pipelining\n", check_capabilities->parser.capa_list->pipelining == true ? "supports" : "does not support");
                return COPY;
            }
            else
                return ERROR;
        }
    }
    shutdown(key->fd, SHUT_RD);
    return ERROR;
}

/*Le envio a origen el comando CAPA */
static unsigned check_capa_write(struct selector_key *key){
    struct pop3 * proxy = ATTACHMENT(key);
    //struct check_capa * check_capabilities = &proxy->origin.capabilities;
    int n = send(key->fd, capa_msg, CAPA_MSG_LEN, MSG_NOSIGNAL);
    if(n > 0){
        log(INFO, "Bytes sent: %d\n", n);
        metrics.bytes_transferred += n;
        if(SELECTOR_SUCCESS == selector_set_interest(key->s, proxy->origin_fd, OP_READ) &&
               SELECTOR_SUCCESS == selector_set_interest_key(key, OP_READ) ) {
            return CHECK_CAPABILITIES;
        }
    }
    shutdown(key->fd, SHUT_RD);
    return ERROR;
}

// COPY

struct copy * copy_ptr(struct selector_key * key) {
    struct copy * c = &ATTACHMENT(key)->client.copy;
    while(*c->fd != key->fd){
        c = c->other;
    }
    return c;
}

static void copy_init(const unsigned state, struct selector_key *key){
     log(INFO, "COPY READ%d\n",1);

    struct copy *c = &ATTACHMENT(key) -> client.copy;
    c->fd = &ATTACHMENT(key)->client_fd;
    c->read_b = ATTACHMENT(key)->write_buffer; //escribe client
    c->write_b = ATTACHMENT(key)->read_buffer;
    c->duplex = OP_READ | OP_WRITE;
    c->other = &ATTACHMENT(key)->origin.copy;
    
    c = &ATTACHMENT(key)->origin.copy;
    
    c->fd = &ATTACHMENT(key)->origin_fd;
    c->read_b = ATTACHMENT(key)->read_buffer; //aca escribe origin
    c->write_b = ATTACHMENT(key)->write_buffer;
    c->duplex = OP_READ | OP_WRITE;
    c->other = &ATTACHMENT(key)->client.copy;
    
    if(args.command == NULL){
    c->fd = &ATTACHMENT(key)->client_fd;
    c->read_b = ATTACHMENT(key)->write_buffer; 
    c->write_b = ATTACHMENT(key)->read_buffer;
    c->duplex = OP_READ | OP_WRITE;
    c->other = &ATTACHMENT(key)->origin.copy;
    
    
    c = &ATTACHMENT(key)->origin.copy;
    
    c->fd = &ATTACHMENT(key)->origin_fd;
    c->read_b = ATTACHMENT(key)->read_buffer;
    c->write_b = ATTACHMENT(key)->write_buffer;
    c->duplex = OP_READ | OP_WRITE;
    c->other = &ATTACHMENT(key)->client.copy;

    // }else{


    // c->fd = &ATTACHMENT(key)->client_fd;
    // c->read_b = ATTACHMENT(key)->write_buffer; 
    // c->write_b = ATTACHMENT(key)->read_buffer;
    // c->duplex = OP_READ | OP_WRITE;
    // c->other = &ATTACHMENT(key)->origin.copy;
    
    
    // c = &ATTACHMENT(key)->origin.copy;
    
    // c->fd = &ATTACHMENT(key)->origin_fd;
    // c->read_b = ATTACHMENT(key)->read_buffer;
    // c->write_b = ATTACHMENT(key)->write_buffer;
    // c->duplex = OP_READ | OP_WRITE;
    // c->other = &ATTACHMENT(key)->filter_in.copy;

    // filter_init(key); // chequear si esto va aca
    // ATTACHMENT(key)->filter_in.filter_state_in = 0;
    // ATTACHMENT(key)->filter_out.filter_state_out = 0;

    // c = &ATTACHMENT(key)->filter_in.copy;
    
    // c->fd = &ATTACHMENT(key)->filter_data.in[W]; 
    // c->read_b = ATTACHMENT(key)->write_buffer; 
    // c->write_b = ATTACHMENT(key)->read_buffer;
    // c->duplex = OP_READ | OP_WRITE;
    // c->other = &ATTACHMENT(key)->filter_out.copy;


    // c = &ATTACHMENT(key)->filter_out.copy;
    
    // c->fd = &ATTACHMENT(key)->filter_data.out[R]; 
    // c->read_b = ATTACHMENT(key)->read_buffer; 
    // c->write_b = ATTACHMENT(key)->write_buffer;
    // c->duplex = OP_READ | OP_WRITE;
    // c->other = &ATTACHMENT(key)->client.copy;



    // want_filter(false,key);
  
    }

  
    
   
    
}



static void want_filter(bool filter, struct selector_key * key){
    if(filter == true && args.command != NULL){

   struct copy *c = &ATTACHMENT(key) -> origin.copy;
    
    c->fd = &ATTACHMENT(key)->origin_fd;
    c->other = &ATTACHMENT(key)->filter_in.copy;

    
    filter_init(key); // chequear si esto va aca
    ATTACHMENT(key)->filter_in.filter_state_in = 0;
    ATTACHMENT(key)->filter_out.filter_state_out = 0;

    c = &ATTACHMENT(key)->filter_in.copy;
    
    c->fd = &ATTACHMENT(key)->filter_data.in[W]; 
    c->read_b = ATTACHMENT(key)->write_buffer; 
    c->write_b = ATTACHMENT(key)->read_buffer;
    c->duplex = OP_READ | OP_WRITE;
    c->other = &ATTACHMENT(key)->filter_out.copy;


    c = &ATTACHMENT(key)->filter_out.copy;
    
    c->fd = &ATTACHMENT(key)->filter_data.out[R]; 
    c->read_b = ATTACHMENT(key)->read_buffer; 
    c->write_b = ATTACHMENT(key)->write_buffer;
    c->duplex = OP_READ | OP_WRITE;
    c->other = &ATTACHMENT(key)->client.copy;



    }else{

    struct copy *c = &ATTACHMENT(key) -> origin.copy;
    
    c->fd = &ATTACHMENT(key)->origin_fd;
    c->other = &ATTACHMENT(key)->client.copy;

    c = &ATTACHMENT(key)->filter_in.copy;
    
    c->read_b = NULL; 
    c->write_b = NULL;


    c = &ATTACHMENT(key)->filter_out.copy;
    
    c->read_b = NULL; 
    c->write_b = NULL;

    }

}

static fd_interest copy_interest(fd_selector s, struct copy *c){ 
    fd_interest ret = OP_NOOP;
    if((c->duplex & OP_READ) && buffer_can_write(c->read_b)){
        ret |= OP_READ; //me subscribo si tengo lugar en el buffer 
    }

    if((c->duplex & OP_WRITE) && buffer_can_read(c->write_b)){
        ret |= OP_WRITE;
    }
    if(SELECTOR_SUCCESS != selector_set_interest(s,*c->fd,ret)){
        abort(); //TODO mensaje de error?
    }

    return ret;
}

static unsigned copy_r(struct selector_key *key){

    struct copy *c = copy_ptr(key); 
    assert(*c->fd == key->fd);
    size_t size;
    ssize_t n;
    buffer *b = c->read_b;
    unsigned ret = COPY; 

    uint8_t *ptr = buffer_write_ptr(b,&size);
    n = recv(key->fd,ptr,size,0);
 

    if(n<=0){
        shutdown(*c->fd,SHUT_RD);
        c->duplex &= -OP_WRITE;
        if(*c->other->fd!=-1){
            shutdown(*c->other->fd,SHUT_WR);
            c->other->duplex &= -OP_WRITE;
        }

    }else{
        buffer_write_adv(b,n); 
        //filter_write(&f->in[W],b);
        //close(f->in[W]);
        //filter_read(&f->out[R],b);
        
    }

    //filter_interest(key->s,f,b,b);
    copy_interest(key->s,c);
    copy_interest(key->s,c->other);

    if(c->duplex == OP_NOOP){
        ret = DONE;
    }

    return ret;
}

static unsigned copy_w(struct selector_key *key){
    struct copy *c = copy_ptr(key); 
    assert(*c->fd == key->fd);
    size_t size;
    ssize_t n;
    buffer *b = c->write_b;
    unsigned ret = COPY; //ESTADO DE RETORNO?
    struct pop3 * proxy = ATTACHMENT(key);


    if(ATTACHMENT(key)->flag_filter == 1){
       ATTACHMENT(key)->flag_filter = 0;
       filter_block(key); 
       state_out = 4;
    }

    if (*c->fd == proxy->origin_fd) {
        bool new_cmd = false;
        struct cmd_parser * parser = &proxy->command_parser;
        cmd_comsume(b, proxy->commands_queue, parser, &new_cmd);
        bool filter = parser->current_cmd.type == CMD_RETR || parser->current_cmd.type == CMD_TOP;
        log(INFO, "Command type %d\n", parser->current_cmd.type);
        want_filter(filter, key);
    }

    uint8_t *  ptr = buffer_read_ptr(b,&size);

    n = send(key->fd,ptr,size,MSG_NOSIGNAL);
    if (n == -1) {
        shutdown(*c->fd,SHUT_WR);
        c->duplex &= -OP_WRITE;
        if(*c->other->fd!=-1){
            shutdown(*c->other->fd,SHUT_RD);
            c->other->duplex &= -OP_READ;
        }
    }else{
        buffer_read_adv(b,n);
    }
    copy_interest(key->s,c);
    copy_interest(key->s,c->other);
    if(c->duplex == OP_NOOP){ //SE CERRARON LOS DOS
        ret = DONE;
    }
    return ret;
}

// SEND_ERROR_MSG
static unsigned write_error_msg(struct selector_key * key) {
    struct pop3 * proxy = ATTACHMENT(key);
    unsigned ret = SEND_ERROR_MSG;

    if(proxy->error_sender.message == NULL)
        return ERROR;
    if(proxy->error_sender.message_length == 0)
        proxy->error_sender.message_length = strlen(proxy->error_sender.message);
        
    char *   ptr  = proxy->error_sender.message + proxy->error_sender.sended_size;
    ssize_t  size = proxy->error_sender.message_length - proxy->error_sender.sended_size;
    ssize_t  n    = send(proxy->client_fd, ptr, size, MSG_NOSIGNAL);
    if(n == -1) {
        shutdown(proxy->client_fd, SHUT_WR);
        ret = ERROR;
    } else {
        proxy->error_sender.sended_size += n;
        if(proxy->error_sender.sended_size == proxy->error_sender.message_length) 
            return ERROR;
    }
    return ret;
}

//filter

static void set_variables(struct pop3 * pop3_struct){
    setenv("POP3FILTER_VERSION", "0.0.0.0", 1);//cambiar la version
    setenv("POP3_SERVER",addressOrigin, 1);//todo revisar
    setenv("POP3_USERNAME",user, 1);
}

static const struct fd_handler filter_handler = {
    .handle_read   = filter_read,
    .handle_write  = filter_write,
    .handle_close  = filter_close,
    .handle_block  = filter_block,
};


static void filter_init(struct selector_key *key){

    ATTACHMENT(key)->headers = buffer_init(MAX_BUFF);
    struct filter *f = &ATTACHMENT(key) -> filter_data;
    //f->fd = &ATTACHMENT(key) -> origin_fd;

    for(int i = 0; i < 2; i++) {
        f->in[i]  = -1;
        f->out[i] = -1;
    }

    if(pipe(f->in)==-1 || pipe(f->out) == -1){
        perror("error creating pipes");
        exit(EXIT_FAILURE);
        return;
    }

    const pid_t pid = fork();


    if(pid == -1){
        perror("creating process");
        exit(EXIT_FAILURE);
        return; //todo estado de error?
    }else if(pid == 0){ // soy el hijo ej cat
        close(f->in[W]); // no quiero escribir en el pipe de escritura si soy el hijo
        close(f->out[R]);
        f->in[W] = f->out[R] == -1;

        dup2(f->in[R],STDIN_FILENO); //"redirect"
        dup2(f->out[W],STDOUT_FILENO);

        set_variables(ATTACHMENT(key));   //setear variables de entorno
        
       if(-1 == execl("/bin/sh", "sh", "-c", args.command, (char *) 0)){
           //escribir sin transformar los mensajes
           abort(); //fix-me
           perror("executing command error");
        //    dup2(f->in[R],STDIN_FILENO); //"redirect"
        //    dup2(f->out[W],STDOUT_FILENO);
        //     while(1){
	    //         int c = getc(stdin);
	    //         if(c < 0) exit(1);
        //         putc(c,stdout);
        //         fflush(stdout);
	    //         }
       }
       

    }else{
        
        close(f->in[R]);
        close(f->out[W]);
        f->in[R] = f->out[W] = -1;
        selector_fd_set_nio(f->in[W]);
        selector_fd_set_nio(f->out[R]);

        selector_register(key->s, f->in[W], &filter_handler, OP_READ, ATTACHMENT(key));
        selector_register(key->s, f->out[R], &filter_handler, OP_WRITE, ATTACHMENT(key));
     

        //int fds[] = {in,out,f->in[W],f->out[R]};
        //serve(fds);
        //filter_interest(key->s,f);
    }



}


static void filter_read(struct selector_key *key){
    struct copy *c = copy_ptr(key);
    assert(*c->fd == key->fd);
    struct filter * f = &ATTACHMENT(key)->filter_data;
    buffer *b = c->read_b;
    //unsigned ret = COPY; 
    uint8_t *ptr;
    uint8_t *header_ptr;
    ssize_t n;
    size_t count = 0;
    size_t count_h = 0;

    
    ptr = buffer_write_ptr(b,&count);
    header_ptr = buffer_read_ptr(ATTACHMENT(key)->headers,&count_h);
    strcpy((char *) ptr, (char *) header_ptr);
    buffer_read_adv(ATTACHMENT(key)->headers,count_h);
    buffer_write_adv(b,(ssize_t) count_h);
    ptr = buffer_write_ptr(b,&count);

    n = read(f->out[R],ptr,count);
    
    state = 0;
    back_to_pop3((char *)ptr);
    //state_out = 4;
   
    ATTACHMENT(key)->flag_filter = 1;

  
    if(n == 0 || n == -1){
        //problema con el filter mensaje de 
        shutdown(*c->fd,SHUT_RD);
        c->duplex &= -OP_WRITE;
        //f->duplex[R] &= -OP_WRITE; //chequear
        if(*c->other->fd!=-1){
            shutdown(*c->other->fd,SHUT_WR);
            c->other->duplex &= -OP_WRITE;
        }
        //f->out[R] = -1;
        //close(f->out[R]); //chequear
    }else{
        buffer_write_adv(b,n);
        metrics.bytes_transferred += n;
    }

    copy_interest(key->s,c);
    copy_interest(key->s,c->other);

    return;
}


static void filter_write(struct selector_key *key){
    struct copy *c = copy_ptr(key); 
    assert(*c->fd == key->fd);
    uint8_t *ptr;
    uint8_t *header_ptr;
    ssize_t n;
    size_t count = 0;
    size_t count_h = 0;
    buffer *b = c->write_b;
    int index = parse_headers(c);

    ptr = buffer_read_ptr(b,&count);
    header_ptr = buffer_write_ptr(ATTACHMENT(key)->headers,&count_h);
    strncpy((char *)header_ptr,(char *)ptr,index);
    buffer_write_adv(ATTACHMENT(key)->headers,index);
    buffer_read_adv(b,index);
    ptr = buffer_read_ptr(b,&count);

    n = write(*c->fd,ptr,count);
    // close(*c->fd);
    // selector_unregister_fd(key->s,*c->fd);

    if(n == -1){
        shutdown(*c->fd,SHUT_WR);
        c->duplex &= -OP_WRITE;
        if(*c->other->fd!=-1){
            shutdown(*c->other->fd,SHUT_RD);
            c->other->duplex &= -OP_READ;
        }
    }else{
        buffer_read_adv(b,n);
    }

    copy_interest(key->s,c);
    copy_interest(key->s,c->other);
    
    return;
}


static void filter_close(struct selector_key *key) {
   

}

static void filter_block(struct selector_key *key) {
      const int fds[] = {
        ATTACHMENT(key)->filter_data.in[W],
        ATTACHMENT(key)->filter_data.out[W],
        ATTACHMENT(key)->filter_data.in[R],
        ATTACHMENT(key)->filter_data.out[R],
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
