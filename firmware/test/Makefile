CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -I../src -Ibuild
OUT    := build
# Multi-source one-shot compiles keep only the last unit's -MMD deps, so track headers explicitly.
HDRS   := $(wildcard ../src/*.h *.h)

TESTS  := test_crc32 test_mech test_console test_gcr test_flux_dec test_trackbuf_ipc test_mech_server test_wtrack \
          test_sp_wire test_ramdisk test_sp_dev test_sp_bus test_fstore test_sp_floppy test_loop

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do ./$(OUT)/$$t || exit 1; done
	@sh ./check_wrtgate.sh

$(OUT)/test_crc32: test_crc32.c ../src/crc32.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_mech: test_mech.c ../src/mech.c ../src/mech_addr.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_loop: test_loop.c ../src/loop.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_console: test_console.c ../src/console.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_gcr: test_gcr.c ../src/gcr.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_flux_dec: test_flux_dec.c ../src/flux_dec.c ../src/gcr.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_trackbuf_ipc: test_trackbuf_ipc.c ../src/trackbuf.c ../src/ipc.c ../src/gcr.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_mech_server: test_mech_server.c ../src/mech_server.c ../src/mech.c ../src/mech_addr.c ../src/flux_dec.c ../src/trackbuf.c ../src/wtrack.c ../src/gcr.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_wtrack: test_wtrack.c ../src/wtrack.c ../src/gcr.c ../src/flux_dec.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_sp_wire: test_sp_wire.c ../src/sp_wire.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_ramdisk: test_ramdisk.c ../src/ramdisk.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_sp_dev: test_sp_dev.c ../src/sp_dev.c ../src/ramdisk.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_sp_bus: test_sp_bus.c ../src/sp_bus.c ../src/sp_dev.c ../src/sp_wire.c ../src/ramdisk.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) -lm

MECH_SRCS := ../src/mech_server.c ../src/mech.c ../src/mech_addr.c ../src/flux_dec.c ../src/trackbuf.c ../src/wtrack.c ../src/gcr.c

$(OUT)/test_fstore: test_fstore.c ../src/fstore.c $(MECH_SRCS) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

$(OUT)/test_sp_floppy: test_sp_floppy.c test_sp_bus.c ../src/sp_bus.c ../src/sp_dev.c ../src/sp_wire.c ../src/ramdisk.c ../src/fstore.c $(MECH_SRCS) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_sp_floppy.c ../src/sp_bus.c ../src/sp_dev.c ../src/sp_wire.c ../src/ramdisk.c ../src/fstore.c $(MECH_SRCS) -lm

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)

.PHONY: all clean
