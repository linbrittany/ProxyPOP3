#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <stdbool.h>
#include <stdint.h>
#include "buffer.h"
#include "queue.h"

#define CMD_QTY 8

typedef enum cmd_type {
    CMD_OTHER = -1,
    CMD_USER = 0,
    CMD_PASS = 1,
    CMD_APOP = 2,
    CMD_LIST = 3,
    CMD_RETR = 4,
    CMD_TOP = 5,
    CMD_UIDL = 6,
    CMD_CAPA = 7,
} cmd_type;

typedef enum cmd_state {
    CMD_TYPE,
    CMD_ARGS,
    CMD_CRLF,
    CMD_ERROR,
} cmd_state;

struct st_command {
    cmd_type type;
    bool is_multiline;
    void * arg;
    char * cmd;
    size_t cmd_size;
    bool indicator;
};

struct cmd_parser {
    size_t length;
    size_t arg_len;
    size_t arg_qty;
    cmd_state state;
    struct st_command current_cmd;
    bool check_cmd[CMD_QTY];
    int to_check;
};

void cmd_parser_init(struct cmd_parser * parser);
void cmd_init(struct st_command * cmd);
cmd_state cmd_parser_feed(struct cmd_parser * parser, struct Queue * queue, const uint8_t b, bool * new_cmd);
cmd_state cmd_comsume(buffer *b, struct Queue * queue, struct cmd_parser *p, bool * new_cmd);
void cmd_destroy(struct st_command *command);
void handle_cmd(struct cmd_parser *p, struct st_command *current_cmd, struct Queue *queue, bool * new_cmd);

#endif
