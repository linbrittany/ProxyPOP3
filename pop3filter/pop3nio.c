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
#include <sys/socket.h>

#include "selector.h"
#include "stm.h"
#include "pop3nio.h"
#include "buffer.h"
#include "netutils.h"
#include "hello_parser.h"
#include "logger.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))
#define MAX_BUFF 4000

struct filter{
    int                 infd[2];
    int                 outfd[2];
    pid_t               pid;
    //char * command;
    fd_interest interest;
    buffer *buff_in,*buff_out;
    
};


struct copy
{
    int *fd;
    buffer *read_b,*write_b;
    struct copy *other;
    fd_interest duplex;
};


typedef struct capabilities {
    //Listado de las capabilities a negociar con origen
    bool pipelining;
} capabilities;


struct check_capa
{
    buffer       *read_b;
    capabilities *capabilities;
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

    /** maquinas de estados */
    struct state_machine stm;

    struct buffer * read_buffer;
    struct buffer * write_buffer;

    /* Persisto si origen soporta una lista de capabilities de POP3 */
    capabilities origin_capabilities;
    
    error_container error_sender;

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

        struct filter filter;
     } origin;

    address_info origin_addr_data;

    struct addrinfo *origin_resolution; //No pisarlo porque hay que liberarlo
    struct addrinfo *curr_origin_resolution;

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

//static void filter_init(const unsigned state, struct selector_key *key);
//static fd_interest filter_interest(fd_selector s, struct filter *f);


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

static struct pop3 * pop3_new(int client_fd, size_t buffer_size, address_info origin_addr_data) {

    struct pop3 * new_pop3 = malloc(sizeof(struct pop3));
    memset(new_pop3, 0, sizeof(struct pop3));
    new_pop3->client_fd = client_fd;
    new_pop3->origin_fd = -1;
    new_pop3->read_buffer = buffer_init(buffer_size);
    new_pop3->write_buffer = buffer_init(buffer_size);
    new_pop3->origin_addr_data = origin_addr_data;

    new_pop3->stm.initial = RESOLVING;
    new_pop3->stm.max_state = ERROR;
    new_pop3->stm.states = client_state_def;

    stm_init(&new_pop3->stm);
    return new_pop3;
}

void pop3_passive_accept(struct selector_key *key) {
 
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    address_info * origin_addr_data = (address_info *) key->data;
    struct pop3 *state = NULL;
    pthread_t tid;

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

    //Crear socket entre proxy y servidor origen y registrarlo para escritura
fail2:
    selector_unregister_fd(key->s, client);
fail:
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
    else if (SELECTOR_SUCCESS == selector_set_interest_key(key, OP_READ)) { //Setear origin para leer
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
    log(DEBUG, "KEY n %ld", n);
    if (n == -1) {
        shutdown(key->fd, SHUT_WR);
        return ERROR;
    }
    else {
        if (hello_is_done(hello->parser.state, 0)) {
            if(SELECTOR_SUCCESS == selector_set_interest(key->s, proxy->origin_fd, OP_WRITE) &&
               SELECTOR_SUCCESS == selector_set_interest_key(key, OP_NOOP))
                return CHECK_CAPABILITIES;
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
}

static const char *capa_msg = "CAPA\n";
static const int CAPA_MSG_LEN = 5;


/*lectura y parseo de la respuesta del origen */
static unsigned check_capa_read(struct selector_key *key){
    struct pop3 * proxy = ATTACHMENT(key);
    struct check_capa * check_capabilities = &proxy->origin.capabilities;
    buffer * buff       = check_capabilities->read_b;
    size_t len;
    uint8_t * write_ptr = buffer_write_ptr(buff, &len);
    ssize_t n = recv(key->fd, write_ptr, len, 0);
    if( n > 0){
        log(INFO, "LEI %ld bytes del ORIGEN.",n);
        if(SELECTOR_SUCCESS == selector_set_interest(key->s, proxy->client_fd, OP_READ) &&
               SELECTOR_SUCCESS == selector_set_interest_key(key, OP_READ))
            return COPY;
        else
            return ERROR;
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
        log(INFO,"ENVIADOS %d BYTES", n);
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
    if (*c->fd == key->fd) {
        //ok
    }else{
        c = c->other;
    }
    return c;
}

static void copy_init(const unsigned state, struct selector_key *key){


    struct copy *c = &ATTACHMENT(key) -> client.copy;

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
    }

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

    uint8_t *ptr = buffer_read_ptr(b,&size);
    n = send(key->fd,ptr,size,MSG_NOSIGNAL);
    if(n==-1){
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
        
    // logDebg("Enviando error: %s", proxy->error_sender.message);
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

/*

static void set_variables(struct pop3 * pop3_struct){

    setenv("POP3FILTER_VERSION", "0.0.0.0", 1);//cambiar la version
    setenv("POP3_SERVER", pop3_struct->origin_addr_data.addr.fqdn, 1);//todo revisar
}



static void filter_init(const unsigned state, struct selector_key *key){
    printf("FILTER_INIT");
    fflush(stdout);
    enum{
        R=0,
        W=1
    };
    struct filter *f = &ATTACHMENT(key) -> origin.filter;


    for(int i = 0; i < 2; i++) {
        f->infd[i]  = -1;
        f->outfd[i] = -1;
    }

    if(pipe(f->infd)==-1 || pipe(f->outfd) == -1){
        perror("error creating pipes");
        exit(EXIT_FAILURE);
        return;
    }

    const pid_t pid = fork();


    if(pid == -1){
        perror("creating process");
        exit(EXIT_FAILURE);
        return; //todo estado de error?
    }else if(pid == 0){
        close(f->infd[W]);
        close(f->outfd[R]);
        f->infd[W] = f->outfd[R] == -1;
        dup2(f->infd[R],STDIN_FILENO);
        dup2(f->outfd[W],STDOUT_FILENO);

        set_variables(ATTACHMENT(key));   //setear variables de entorno
       if(-1 == execl("/bin/sh", "sh", "-c", args.command, (char *) 0)){
           //llevar a estado de error?
           perror("executing command");
           close(f->infd[R]);
           close(f->outfd[W]);
       }

    }else{
        close(f->infd[R]);
        close(f->outfd[W]);
        f->infd[R] = f->outfd[R] = -1;

        //int fds[] = {infd,outfd,f->infd[W],f->outfd[R]};
        //serve(fds);
        f->interest = filter_interest(key->s,f);
    }



}

int fd_select(int fd[2], int n){
     printf("FD_SELECT");
    fflush(stdout);
    for(int i = 0; i<2; i++){
        if(fd[i] > n){
            n = fd[i];
        }
        
    }
    return n;
}
static fd_interest filter_interest(fd_selector s, struct filter *f){
    
       enum{
        R=0,
        W=1
    };

    int nfds = fd_select(f->infd,0);
    nfds = fd_select(f->outfd,nfds);

    fd_interest ret = OP_NOOP;
    f->buff_in = buffer_init(MAX_BUFF);
    f->buff_out = buffer_init(MAX_BUFF);

    fd_set readfd, writefd;
    FD_ZERO(&readfd); 
    FD_ZERO(&writefd);

   //Hasta aca funciona
    if(f->infd[R]!=-1&& buffer_can_write(f->buff_in)){
        ret |= OP_READ; //me subscribo si tengo lugar en el buffer 
    }

    if(f->outfd[R]!=-1&& buffer_can_write(f->buff_out)){
        ret |= OP_READ;
    }
    if(f->outfd[W]!=-1&& buffer_can_read(f->buff_in)){
        ret |= OP_WRITE;
    }
    if(f->infd[W]!=-1&& buffer_can_read(f->buff_out)){
        ret |= OP_WRITE;
    }
    
    if(-1==f->infd[R] && -1!=f->outfd[W] && !buffer_can_read(f->buff_in)){
        close(f->outfd[W]);
        f->outfd[W] = -1;
    }

    if(-1 == f->outfd[R] && -1 != f->infd[W] && !buffer_can_read(f->buff_out)){
        close(f->infd[W]);
        f->infd[W] = -1;
    }

      if(SELECTOR_SUCCESS != selector_set_interest(s,nfds,ret)){ //chequear
        abort(); //TODO mensaje de error?
    }

    if(-1 == f->infd[R] && -1 == f->outfd[R] && -1 == f->outfd[W] && -1 == f->infd[W]){
        return -1; //?
    }

    return ret;
}*/



