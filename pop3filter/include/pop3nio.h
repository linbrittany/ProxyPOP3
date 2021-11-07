#ifndef POP3_NIO_H
#define POP3_NIO_H

struct proxy_args
{
    // int socketO;
    // int socketC;
    char *stderr_file_path;
    char *listen_pop3_admin_address;
    char *listen_pop3_address;
};

void pop3_passive_accept(struct selector_key *key);

#endif
