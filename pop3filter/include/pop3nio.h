#ifndef POP3_NIO_H
#define POP3_NIO_H

struct proxy_args {
    size_t buffer_size;
    char *stderr_file_path;
    char *listen_pop3_admin_address;
    char *listen_pop3_address;
    char *listen_origin_address;

    union 
    {
        struct hello_st hello;
    } client;
    
};

struct hello_st
{

    int *fd;
    buffer *client_b;
};


struct proxy_args args;

void pop3_passive_accept(struct selector_key *key);

#endif
