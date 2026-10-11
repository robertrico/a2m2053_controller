#include <string.h>
#include "test.h"
#include "console.h"

static con_cmd_t P(const char *s) { return console_parse(s); }

int main(void) {
    CHECK(P("").op == CON_NONE);
    CHECK(P("   \r\n").op == CON_NONE);
    CHECK(P("bogus").op == CON_BAD);
    CHECK(P("help").op == CON_HELP);
    CHECK(P("oe on").op == CON_OE && P("oe on").arg[0] == 1);
    CHECK(P("oe off").arg[0] == 0);
    CHECK(P("oe maybe").op == CON_BAD);
    CHECK(P("enbl on").op == CON_ENBL);
    CHECK(P("motor off").op == CON_MOTOR);
    CHECK(P("status").op == CON_STATUS && P("status").argc == 0);
    CHECK(P("status 14").argc == 1 && P("status 14").arg[0] == 14);
    CHECK(P("status 16").op == CON_BAD);
    CHECK(P("cmd 0xE").op == CON_CMD && P("cmd 0xE").arg[0] == 14);
    CHECK(P("cmd 16").op == CON_BAD);
    CHECK(P("seek 12").op == CON_SEEK && P("seek 12").arg[0] == 12);
    CHECK(P("seek 80").op == CON_BAD);
    CHECK(P("seek -1").op == CON_BAD);
    CHECK(P("seek 1 2").op == CON_BAD);
    CHECK(P("seek x").op == CON_BAD);
    CHECK(P("head 1").op == CON_HEAD);
    CHECK(P("head 2").op == CON_BAD);
    CHECK(P("recal").op == CON_RECAL);
    CHECK(P("recal 3").op == CON_BAD);
    CHECK(P("eject").op == CON_EJECT);
    CHECK(P("tach").op == CON_TACH);
    CHECK(P("settle 25").op == CON_SETTLE && P("settle 25").arg[0] == 25);
    CHECK(P("settle 0").op == CON_BAD);
    con_cmd_t c = P("capture 3 1 50000");
    CHECK(c.op == CON_CAPTURE && c.argc == 3 && c.arg[0] == 3 && c.arg[1] == 1 && c.arg[2] == 50000);
    CHECK(P("capture 3 2 5").op == CON_BAD);
    CHECK(P("capture 3 1 100001").op == CON_BAD);
    CHECK(P("capture 3 1").op == CON_BAD);
    char longline[200];
    memset(longline, 'a', sizeof longline - 1);
    longline[sizeof longline - 1] = 0;
    CHECK(P(longline).op == CON_BAD);
    CHECK(P("server on").op == CON_SERVER && P("server on").arg[0] == 1);
    CHECK(P("server off").arg[0] == 0);
    CHECK(P("server").op == CON_BAD);
    CHECK(P("readblk 0").op == CON_READBLK && P("readblk 1599").arg[0] == 1599);
    CHECK(P("readblk 1600").op == CON_BAD && P("readblk -1").op == CON_BAD);
    CHECK(P("readdisk").op == CON_READDISK && P("readdisk 1").op == CON_BAD);
    CHECK(strlen(console_help()) > 20);
    TEST_END();
}
