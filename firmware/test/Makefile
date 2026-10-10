CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -I../src -Ibuild
OUT    := build

TESTS  := test_crc32 test_mech test_console

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do ./$(OUT)/$$t || exit 1; done
	@sh ./check_wrtgate.sh

$(OUT)/test_crc32: test_crc32.c ../src/crc32.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $^

$(OUT)/test_mech: test_mech.c ../src/mech.c ../src/mech_addr.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $^

$(OUT)/test_console: test_console.c ../src/console.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $^

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)

.PHONY: all clean
