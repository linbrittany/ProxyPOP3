#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>

#include "adminnio.h"
#include "logger.h"
#include "pop3nio.h"

#define BUFFER_MAX_SIZE 1024
#define COMMANDS_QTY 9

extern struct proxy_args args;

// gasti
int parse(char *buffer);

typedef enum status_code{
    OK_RESPONSE         = 200,
    INVALID_ARGUMENT    = 100,
    UNAUTHORIZED        = 101,
    UNSUPPORTED_COMMAND = 102,
} status_code;

typedef struct command_action {
    char * command;
    size_t args_qty;
    void (*func) (char buffer[]);
} command_action;

void get_buffer_size(char buffer []);

// pushea gasti
command_action commands[COMMANDS_QTY] = {
    {.command = "stats"         , .args_qty = 0, .func = &get_buffer_size},
    {.command = "get_buff_size" , .args_qty = 0, .func = &get_buffer_size},
    {.command = "set_buff_size" , .args_qty = 1, .func = &get_buffer_size},
    {.command = "get_timeout"   , .args_qty = 0, .func = &get_buffer_size},
    {.command = "set_timeout"   , .args_qty = 1, .func = &get_buffer_size},
    {.command = "get_error_file", .args_qty = 0, .func = &get_buffer_size},
    {.command = "set_error_file", .args_qty = 1, .func = &get_buffer_size},
    {.command = "get_filter"    , .args_qty = 0, .func = &get_buffer_size},
    {.command = "set_filter"    , .args_qty = 1, .func = &get_buffer_size},
};
// 

void admin_passive_accept(struct selector_key *key) {
    char buffer[BUFFER_MAX_SIZE] = {0};
    unsigned int len, n;
    struct sockaddr_in6 clntAddr;

    n = recvfrom(key->fd, buffer, BUFFER_MAX_SIZE, 0, (struct sockaddr *) &clntAddr, &len);
    if ( buffer[n-1] == '\n') // Por si lo estan probando con netcat, en modo interactivo
		n--;
	buffer[n] = '\0';
	log(DEBUG, "UDP received:%s", buffer);

    return;
}

// pushea gasti
int parse(char *buffer) {
    // TODO buffer cmp contra admin token 
    const char s[2] = " ";
    char *token;

    int command_index = -1;

    char *credToken = args.admin_credential;

    token = strtok(buffer,s);

    if ( strcmp(credToken,token) != 0 ) {
        return -1;
    }

    // TODO buffer cmp conntra commandos
    token = strtok(NULL, s);

    for (int i = 0 ; i < COMMANDS_QTY ; i++) {
        if ( strcmp(credToken,token) == 0 ) {
            command_index = i;
            break;
        }
    }
    if (command_index == -1)
        return command_index;

    // TODO validar args_qty

    size_t args_qty = 0;
    token = strtok(NULL, s);
    while ( token != NULL ) {
        if ( args_qty > commands[command_index].args_qty ) {
            return -1;
        }
        token = strtok(NULL, s);
    }

    return command_index;
}

// gasti 
status_code set_buffer_size(char * arg, char buffer []) {
    int new_size = atoi(arg);
    if (new_size < 0) {
        return INVALID_ARGUMENT;
    }
    args.buffer_size = new_size;
    sprintf(buffer,"new size set to: %d", new_size);
    return OK_RESPONSE;
}

// gasti <3
// TODO return void?
void get_buffer_size(char buffer []) {
    sprintf(buffer,"Buffer size value: %zu",args.buffer_size);
}

// status_code set_timeout(char *arg, char buffer[]) {

// }

// // TODO return void?
// status_code get_timeout(char buffer []) {
//     sprintf(buffer,"Timeout value: %d",); // no hay timeout ni idea q paso aca
//     return OK_RESPONSE
// }


