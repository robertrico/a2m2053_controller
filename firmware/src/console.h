#pragma once
#include <stdbool.h>

#define CON_LINE_MAX     80
#define CON_CAPTURE_MAX  100000

typedef enum {
    CON_NONE, CON_BAD, CON_HELP, CON_OE, CON_ENBL, CON_MOTOR, CON_STATUS, CON_CMD,
    CON_RECAL, CON_SEEK, CON_HEAD, CON_EJECT, CON_TACH, CON_SETTLE, CON_CAPTURE,
    CON_SERVER, CON_READBLK, CON_READDISK,
} con_op_t;

typedef struct {
    con_op_t op;
    int argc;
    long arg[3];
} con_cmd_t;

con_cmd_t console_parse(const char *line);
const char *console_help(void);
