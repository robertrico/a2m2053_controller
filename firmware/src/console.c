#include "console.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    con_op_t op;
    int nargs;          // -1 = zero or one argument, -2 = zero or two
    bool onoff;
    long lo[3], hi[3];
} con_spec_t;

static const con_spec_t specs[] = {
    {"help",    CON_HELP,     0, false, {0},       {0}},
    {"oe",      CON_OE,       1, true,  {0},       {0}},
    {"enbl",    CON_ENBL,     1, true,  {0},       {0}},
    {"motor",   CON_MOTOR,    1, true,  {0},       {0}},
    {"status",  CON_STATUS,  -1, false, {0},       {15}},
    {"cmd",     CON_CMD,      1, false, {0},       {15}},
    {"recal",   CON_RECAL,    0, false, {0},       {0}},
    {"seek",    CON_SEEK,     1, false, {0},       {79}},
    {"head",    CON_HEAD,     1, false, {0},       {1}},
    {"eject",   CON_EJECT,    0, false, {0},       {0}},
    {"tach",    CON_TACH,     0, false, {0},       {0}},
    {"settle",  CON_SETTLE,   1, false, {1},       {1000}},
    {"capture", CON_CAPTURE,  3, false, {0, 0, 1}, {79, 1, CON_CAPTURE_MAX}},
    {"server",   CON_SERVER,   1, true,  {0},       {0}},
    {"readblk",  CON_READBLK,  1, false, {0},       {1599}},
    {"readdisk", CON_READDISK, 0, false, {0},       {0}},
    {"writeblk", CON_WRITEBLK, 1, false, {0},       {1599}},
    {"format",   CON_FORMAT,  -2, false, {0, 0},    {79, 1}},
};

static bool parse_long(const char *s, long lo, long hi, long *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 0);
    if (errno || end == s || *end || v < lo || v > hi) return false;
    *out = v;
    return true;
}

static bool parse_onoff(const char *s, long *out) {
    if (strcmp(s, "on") == 0) { *out = 1; return true; }
    if (strcmp(s, "off") == 0) { *out = 0; return true; }
    return false;
}

con_cmd_t console_parse(const char *line) {
    con_cmd_t c = { CON_BAD, 0, {0, 0, 0} };
    char buf[CON_LINE_MAX];
    if (strlen(line) >= sizeof buf) return c;
    strcpy(buf, line);
    char *tok[5];
    int nt = 0;
    for (char *t = strtok(buf, " \t\r\n"); t; t = strtok(NULL, " \t\r\n")) {
        if (nt == 5) return c;
        tok[nt++] = t;
    }
    if (nt == 0) { c.op = CON_NONE; return c; }
    for (size_t i = 0; i < sizeof specs / sizeof specs[0]; i++) {
        const con_spec_t *sp = &specs[i];
        if (strcmp(tok[0], sp->name) != 0) continue;
        int given = nt - 1;
        bool count_ok = sp->nargs == -1 ? given <= 1
                      : sp->nargs == -2 ? (given == 0 || given == 2)
                      : given == sp->nargs;
        if (!count_ok) return c;
        for (int k = 0; k < given; k++) {
            bool ok = sp->onoff ? parse_onoff(tok[k + 1], &c.arg[k])
                                : parse_long(tok[k + 1], sp->lo[k], sp->hi[k], &c.arg[k]);
            if (!ok) return c;
        }
        c.op = sp->op;
        c.argc = given;
        return c;
    }
    return c;
}

const char *console_help(void) {
    return "help | oe on|off | enbl on|off | status [0-15] | cmd <0-15> | motor on|off\n"
           "recal | seek <0-79> | head <0|1> | eject | tach | settle <us> | capture <t> <s> <n>\n"
           "server on|off | readblk <0-1599> | readdisk\n"
           "writeblk <0-1599> (then 512 raw bytes) | format [<0-79> <0|1>]";
}
