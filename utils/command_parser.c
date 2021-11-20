#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "command_parser.h"
#include "buffer.h"
#include "logger.h"

#define MAX_CMD_SIZE 512
#define MAX_ARG_SIZE 40

static const char * crlf_msg = "\r\n";

struct command_info {
    cmd_type type;
    char * command;
    size_t len;
    size_t max_args;
    size_t min_args; //si argumento fijo, min-args = max_args
};

static const struct command_info user_commands[] = {
    {
        .type = CMD_USER, .command = "USER", .len = 4, .max_args = 1, .min_args = 1 //TODO: analizar la posibilidad de que el username tenga espacios
    } , {
        .type = CMD_PASS, .command = "PASS", .len = 4, .max_args = 1, .min_args = 1
    } , {
        .type = CMD_APOP, .command = "APOP", .len = 4, .max_args = 2, .min_args = 2
    } , {
        .type = CMD_LIST, .command = "LIST", .len = 4, .max_args = 0, .min_args = 1
    } , {
        .type = CMD_RETR, .command = "RETR", .len = 4, .max_args = 1, .min_args = 1
    } , {
        .type = CMD_TOP, .command = "TOP", .len = 3, .max_args = 2, .min_args = 2
    } , {
        .type = CMD_UIDL, .command = "UIDL", .len = 4, .max_args = 0, .min_args = 1
    } , {
        .type = CMD_CAPA, .command = "CAPA", .len = 4, .max_args = 0, .min_args = 0
    }
};

void cmd_parser_init(struct cmd_parser * parser) {
    parser->state = CMD_TYPE;
    parser->length = 0;
    parser->arg_qty = 0;
    parser->arg_len = 0;
}

void cmd_init(struct st_command * cmd) {
    cmd->type = CMD_OTHER;
    cmd->arg = NULL;
}

extern cmd_state cmd_parser_feed(struct cmd_parser * parser, const uint8_t b, bool * new_cmd) {
    struct st_command * command_info = &parser->current_cmd;
    
    if (parser->length == 0) {
        cmd_init(command_info);
        parser->arg_qty = 0; //vuelvo a setear a cero
        parser->arg_len = 0;
        parser->to_check = CMD_QTY;
        for (int i = 0 ; i < CMD_QTY ; i++) {
            parser->check_cmd[i] = true;
        }
    }

    switch (parser->state) {
        case CMD_TYPE:
            if (b == crlf_msg[1]) {
                parser->state = CMD_ERROR;
                handle_cmd(parser, command_info, new_cmd);
                break;
            }
            else {
                for (int i = 0 ; i < CMD_QTY ; i++) {
                    if (parser->check_cmd[i]) { //si chequeo comando
                        if (toupper(b) != user_commands[i].command[parser->length]) {
                            parser->check_cmd[i] = false;
                            parser->to_check--;
                        }
                        else if (parser->length == user_commands[i].len - 1) {
                            command_info->type = user_commands[i].type;
                            if (user_commands[i].max_args > 0) {
                                parser->state = CMD_ARGS;
                                if (command_info->type == CMD_USER || command_info->type == CMD_APOP) {
                                    command_info->arg = malloc(sizeof(uint8_t) * (MAX_ARG_SIZE + 1)); //guardo autenticacion
                                }
                            }
                            else {
                                parser->state = CMD_CRLF;
                            }
                            break;
                        }
                    }
                }
                if (parser->to_check == 0) {
                    parser->state = CMD_ERROR;
                }
            }
            break;
        case CMD_ARGS:
            if (b == ' ') {
                if (parser->arg_qty == user_commands[parser->current_cmd.type].max_args) {
                    parser->state = CMD_ERROR;
                } else if (parser->arg_len == 0) { //primer argumento
                    parser->arg_len++;
                } else if (parser->arg_len > 1 && parser->arg_qty < user_commands[parser->current_cmd.type].max_args) {
                    parser->arg_qty++;
                    parser->arg_len = 1;
                }
            } else if (b != crlf_msg[0] && b != crlf_msg[1]) {
                if (parser->arg_len == 0) {
                    parser->state = CMD_ERROR;
                }
                else {
                    parser->arg_len++;
                    if (command_info->type == CMD_USER || command_info->type == CMD_APOP) {
                        ((uint8_t *)command_info->arg)[parser->arg_len - 1] = b;
                    }
                }
            } else if (b == crlf_msg[0]) {
                if (parser->arg_len > 1) {
                    parser->arg_qty++;
                }
                if (command_info->type == CMD_USER || command_info->type == CMD_APOP) {
                    ((uint8_t *)command_info->arg)[parser->arg_len - 1] = 0;
                }
                if (parser->arg_qty <= user_commands[parser->current_cmd.type].max_args && parser->arg_qty >= user_commands[parser->current_cmd.type].min_args) {
                    parser->state = CMD_CRLF;
                }
                else {
                    parser->state = CMD_ERROR;
                }
            } else if (b == crlf_msg[1]) {
                if (command_info->type == CMD_USER || command_info->type == CMD_APOP) {
                    ((uint8_t *)command_info->arg)[parser->arg_len - 1] = 0;
                }
                if (parser->arg_len > 1) {
                    parser->arg_qty++;
                }
                if (parser->arg_qty <= user_commands[parser->current_cmd.type].max_args && parser->arg_qty >= user_commands[parser->current_cmd.type].min_args) {
                    handle_cmd(parser, command_info, new_cmd);
                }
                else {
                    parser->state = CMD_ERROR;
                    handle_cmd(parser, command_info, new_cmd);
                }
            }
            else {
                parser->state = CMD_ERROR;
            }
            break;
        case CMD_CRLF: 
            if (b == crlf_msg[1]) {
                handle_cmd(parser, command_info, new_cmd);
            }
            else {
                parser->state = CMD_ERROR;
            }
            break;
        case CMD_ERROR: {
            if (b == crlf_msg[1]) {
                handle_cmd(parser, command_info, new_cmd);
            }
            break;
        }
        default:
            log(ERR, "Command parser: cannot recognize state %d\n", parser->state);
            break;
    }
    if (parser->length++ == MAX_CMD_SIZE) {
        parser->state = CMD_ERROR;
    }
    log(INFO, "Current command parser state: %d\n", parser->state);
    return parser->state;
}

extern cmd_state cmd_comsume(buffer *b, struct cmd_parser *p, bool * new_cmd) {
    cmd_state st = p->state;
    while(buffer_can_parse(b)) {
        const uint8_t c = buffer_parse(b);
        st = cmd_parser_feed(p, c, new_cmd);
        if (*new_cmd) {
            break;
        }
    }
    return st;
}

static bool is_multiline(struct st_command *command, size_t arg_qty) {
    if (command->type == CMD_LIST || command->type == CMD_UIDL) {
        return arg_qty == 0;
    }
    if (command->type == CMD_TOP) {
        return arg_qty == 2;
    }
    if (command->type == CMD_RETR) {
        return arg_qty == 1;
    }
    return command->type == CMD_CAPA;
}

void handle_cmd(struct cmd_parser *p, struct st_command *current_cmd, bool * new_cmd) {
    if (p->state == CMD_ERROR) {
        current_cmd->type = CMD_OTHER;
        if (current_cmd->arg != NULL) {
            free(current_cmd->arg);
            current_cmd->arg = NULL;
        }    
    }
    current_cmd->is_multiline = is_multiline(current_cmd, p->arg_qty);
    //agregar a cola
    *new_cmd = true;
    p->state = CMD_TYPE;
    p->length = 0;
}
