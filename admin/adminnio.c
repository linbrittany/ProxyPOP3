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

int parse( char *buffer, char to_ret []);

typedef enum status_code{
    OK_RESPONSE         = 200,
    INVALID_ARGUMENT    = 100,
    UNAUTHORIZED        = 101,
    UNSUPPORTED_COMMAND = 102,
} status_code;

typedef union {
    void (*getter) (char buffer[]);
    status_code (*setter) (char *arg,char buffer[]);
}func;

typedef struct command_action {
    char * command;
    size_t args_qty;
    func function;
} command_action;

void get_buffer_size(char buffer []);
status_code set_buffer_size(char * arg, char buffer []);

command_action commands[COMMANDS_QTY] = {
    {.command = "stats"         , .args_qty = 0, .function = {.getter = &get_buffer_size}},
    {.command = "get_buff_size" , .args_qty = 0, .function = {.getter = &get_buffer_size}},
    {.command = "set_buff_size" , .args_qty = 1, .function = {.setter = &set_buffer_size}},
    {.command = "get_timeout"   , .args_qty = 0, .function = {.getter = &get_buffer_size}},
    {.command = "set_timeout"   , .args_qty = 1, .function = {.getter = &get_buffer_size}},
    {.command = "get_error_file", .args_qty = 0, .function = {.getter = &get_buffer_size}},
    {.command = "set_error_file", .args_qty = 1, .function = {.getter = &get_buffer_size}},
    {.command = "get_filter"    , .args_qty = 0, .function = {.getter = &get_buffer_size}},
    {.command = "set_filter"    , .args_qty = 1, .function = {.getter = &get_buffer_size}},
};

void admin_passive_accept(struct selector_key *key) {
    char buffer[BUFFER_MAX_SIZE] = {0};
    unsigned int len, n;
    struct sockaddr_in6 clntAddr;

    n = recvfrom(key->fd, buffer, BUFFER_MAX_SIZE, 0, (struct sockaddr *) &clntAddr, &len);
    if (buffer[n-1] == '\n') // Por si lo estan probando con netcat, en modo interactivo
		n--;
	buffer[n] = '\0';
	log(INFO, "UDP received:%s", buffer);

    char to_ret[BUFFER_MAX_SIZE] = {0};
    int cmd = 0 ;
    cmd = parse(buffer, to_ret);
    if (cmd >= 0) {
        if (commands[cmd].function.setter != NULL) {
            commands[cmd].function.setter(buffer,to_ret);
        } else {
            commands[cmd].function.getter(to_ret);
        }
    }

    log(DEBUG,"%lu",strlen(to_ret));
    sendto(key->fd, to_ret, strlen(to_ret), 0, (const struct sockaddr *) &clntAddr, len);

    return;
}

int adv(const char *buffer) {
    int i;
    for ( i = 0; buffer[i] != ' ' && buffer[i] != 0; i++);
    return buffer[i] == 0?i : i+1;
}

int strcmp_custom(char *str1,char *str2) {
    for (int i = 0; str1[i] != 0 && str2[i] != 0 && str2[i] != ' '; i++) {
        if (str1[i] != str2[i]) return -1;
    }
    return 0;
}

int parse( char *buffer, char to_ret []) { 
    int indicator = 0 ;
    size_t token_count = 0;
    int command_index = -1;

    char *credToken = args.admin_credential;

    // indicator = adv(buffer);
    log(INFO,"%s",buffer);
    if (strcmp_custom(credToken, buffer) != 0 ) {
        sprintf(to_ret, "Please enter valid token\n");
        return -1;
    }

    indicator += adv(buffer+indicator);
    printf("token %s %d\n", buffer+indicator,indicator);
    if ( buffer[indicator] == 0) {
        sprintf(to_ret, "Please enter command\n");
        return -1;
    }

    for (int i = 0 ; i < COMMANDS_QTY ; i++) {
        if (strcmp(commands[i].command, buffer+indicator) == 0 ) {
            command_index = i;
            break;
        }
    }

    if (command_index == -1) {
        sprintf(to_ret, "Please enter valid command\n");
        return command_index;
    }

    indicator += adv(buffer+indicator);
    while (buffer[indicator] != 0) {
        token_count++;
        if (token_count > commands[command_index].args_qty) {
            sprintf(to_ret, "INVALID ARGS");
            return -1;
        }
        indicator += adv(buffer+indicator);
    }

    return command_index;
}

status_code set_buffer_size(char * arg, char buffer []) {
    const char s[2] = " ";
    char *token;
    token = strtok(buffer,s);
    token = strtok(NULL, s);
    token = strtok(NULL, s);

    int new_size = atoi(token);
    if (new_size < 0) {
        return INVALID_ARGUMENT;
    }
    args.buffer_size = new_size;
    sprintf(buffer,"New buffer size set to: %d", new_size);
    return OK_RESPONSE;
}


void get_buffer_size(char buffer []) {
    log(DEBUG,"BUENAS FUNCIONA :%s",buffer);
    sprintf(buffer, "Buffer size value: %zu", args.buffer_size);
}

// status_code set_timeout(char *arg, char buffer[]) {

// }

// // TODO return void?
// status_code get_timeout(char buffer []) {
//     sprintf(buffer,"Timeout value: %d",); // no hay timeout ni idea q paso aca
//     return OK_RESPONSE
// }


