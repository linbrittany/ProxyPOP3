#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "adminnio.h"
#include "logger.h"
#include "pop3nio.h"

#define BUFFER_MAX_SIZE 1024
#define COMMANDS_QTY 8

extern struct proxy_args args;
extern struct proxy_metrics metrics;
extern struct selector_init conf;

int parse( char *buffer, char to_ret []);

typedef enum status_code{
    OK_RESPONSE         = 200,
    INVALID_ARGUMENT    = 100,
    UNAUTHORIZED        = 101,
    UNSUPPORTED_COMMAND = 102,
} status_code;

typedef union {
    void (*getter) (char *);
    status_code (*setter) (char *,char *);
}func;

typedef struct command_action {
    char * command;
    size_t args_qty;
    func function;
} command_action;

void get_buffer_size(char *to_ret);
status_code set_buffer_size(char * arg, char *to_ret);
void get_stats(char *to_ret);
void get_error_file(char *to_ret);
status_code set_error_file(char * arg, char to_ret[]);
void get_timeout(char *to_ret);
void get_help(char to_ret[]);
status_code set_timeout(char *arg, char *buffer);

command_action commands[COMMANDS_QTY] = {
    {.command = "stats"         , .args_qty = 0, .function = {.getter = &get_stats}      },
    {.command = "help"          , .args_qty = 0, .function = {.getter = &get_help}       },
    {.command = "getbuffsize" , .args_qty = 0, .function = {.getter = &get_buffer_size}},
    {.command = "setbuffsize" , .args_qty = 1, .function = {.setter = &set_buffer_size}},
    {.command = "gettimeout"   , .args_qty = 0, .function = {.getter = &get_timeout}   },
    {.command = "settimeout"   , .args_qty = 2, .function = {.setter = &set_timeout}   },
    {.command = "geterrorfile", .args_qty = 0, .function = {.getter = &get_error_file }},
    {.command = "seterrorfile", .args_qty = 1, .function = {.setter = &set_error_file}},
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
    // log(DEBUG,"COMMAND %d\n",cmd);
    if (cmd >= 0) {
        if (commands[cmd].args_qty > 0) {
            commands[cmd].function.setter(buffer,to_ret);
        } else {
            commands[cmd].function.getter(to_ret);
        }
    }

    // log(DEBUG,"EXECUTED %d\n",cmd);

    sendto(key->fd, to_ret, strlen(to_ret), 0, (const struct sockaddr *) &clntAddr, len);


    // log(DEBUG,"EXECUTED %s %zd\n",to_ret,sendBytes);

    return;
}

int adv(const char *buffer) {
    int i;
    for ( i = 0; buffer[i] != ' ' && buffer[i] != 0; i++);
    return buffer[i] == 0?i : i+1;
}

int strcmp_custom(char *str1,char *str2) {
    int i;
    for ( i = 0; str1[i] != 0 && str2[i] != 0 && str2[i] != ' '; i++) {
        if (str1[i] != str2[i]) return -1;
    }
    if (str1[i] == 0 && (str2[i] == 0 || str2[i] == ' ') ) return 0;
    return -1;
}

int parse( char *buffer, char to_ret []) { 
    int indicator = 0 ;
    size_t token_count = 0;
    int command_index = -1;

    char *credToken = args.admin_credential;

    // indicator = adv(buffer);
    if (strcmp_custom(credToken, buffer) != 0 ) {
        sprintf(to_ret, "Please enter valid token\n");
        return -1;
    }

    indicator += adv(buffer+indicator);
    if ( buffer[indicator] == 0) {
        sprintf(to_ret, "Please enter command\n");
        return -1;
    }

    for (int i = 0 ; i < COMMANDS_QTY ; i++) {
        if (strcmp_custom(commands[i].command, buffer+indicator) == 0 ) {
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
            sprintf(to_ret, "Please use valid arguments\n");
            return -1;
        }
        indicator += adv(buffer+indicator);
    }

    return command_index;
}

status_code set_buffer_size(char * arg, char to_ret[]) {
    const char s[2] = " ";
    char *token;
    token = strtok(arg,s);
    token = strtok(NULL, s);
    token = strtok(NULL, s);

    int new_size = atoi(token);
    if (new_size < 0) {
        return INVALID_ARGUMENT;
    }
    args.buffer_size = new_size;
    sprintf(to_ret,"New buffer size set to: %d\n", new_size);
    return OK_RESPONSE;
}


void get_buffer_size(char *to_ret) {
    sprintf(to_ret, "Buffer size value: %zu\n", args.buffer_size);
}

void get_stats(char *to_ret) {
    sprintf(to_ret,"Active Connections: %llu\nTotal Connnections: %llu\nTotal Bytes Transfered: %llu\n", 
        metrics.active_connections, metrics.total_connections, metrics.bytes_transferred);
}

void get_error_file(char *to_ret) {
    sprintf(to_ret, "Error file: %s\n", args.stderr_file_path);
}

status_code set_error_file(char * arg, char to_ret[]) {
    FILE *file;
    int advance = adv(arg);
    advance += adv(arg+advance);

    if ((file = fopen(arg+advance,"w")) == NULL) {
        sprintf(to_ret,"This file cannot be open\nFile: %s\n",arg+advance);
        return INVALID_ARGUMENT;
    }
    args.stderr_file_path = calloc(strlen(arg+advance)+1,sizeof(char)); //TODO FREE In after when all close add variable to know if need free
    memcpy(args.stderr_file_path,arg+advance,strlen(arg+advance));
    sprintf(to_ret,"New error file is: %s\n",arg+advance);
    dup2(fileno(file),STDERR_FILENO);
    // fclose(file);   
    return OK_RESPONSE;
}

void get_help(char to_ret[]) {
    sprintf(to_ret, 
    "Help (try one of this commands):\nstats: print proxy's metrics\ngetbuffsize: print current buffer size\nsetbuffsize: set new buffer size\ngettimeout: print current timeout\nsettimeout: set new timeout\ngeterrorfile: print current error file\nseterrorfile: set new error file\n");
}

status_code set_timeout(char *arg, char *to_ret) {
    int advance = adv(arg);
    advance += adv(arg+advance);

    long s = atol(arg+advance);
    advance += adv(arg+advance);
    long ns = atol(arg+advance);

    if (s <= 0 && ns <= 0 ) {
        sprintf(to_ret,"You must enter a number greather than 0\n");
        return INVALID_ARGUMENT;
    }

    conf.select_timeout.tv_sec = s;
    conf.select_timeout.tv_nsec = ns;

    get_timeout(to_ret);

    return OK_RESPONSE;
}

void get_timeout(char *to_ret) {
    sprintf(to_ret,"Timeout value:\nSeconds: %lu\nNano Seconds: %lu\n",conf.select_timeout.tv_sec,conf.select_timeout.tv_nsec);
}
