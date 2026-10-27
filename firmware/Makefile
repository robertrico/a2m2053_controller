PICO_SDK_PATH ?= $(HOME)/Development/pico-sdk
export PICO_SDK_PATH
BUILD   := build
ELF     := $(BUILD)/a2m_bringup.elf

# SWD via picoprobe / Debug Probe (CMSIS-DAP). USE_CORE 0: attach to core 0 only;
# in SMP mode OpenOCD leaves core 1 debug-halted after programming (M4 bench).
# Default: Raspberry Pi's prebuilt
# OpenOCD (pico-sdk-tools v2.3.1-0, has target/rp2350.cfg). Override with OPENOCD=.
OPENOCD ?= $(HOME)/Development/tooling/openocd-rpi/current/openocd
OCD_CFG := -f interface/cmsis-dap.cfg -c "set USE_CORE 0" -f target/rp2350.cfg -c "adapter speed 5000"
GDB     ?= arm-none-eabi-gdb

all: $(BUILD)/build.ninja
	cmake --build $(BUILD)

$(BUILD)/build.ninja:
	cmake -B $(BUILD) -S . -G Ninja

test:
	$(MAKE) -C test

# Hardware targets: drive the board over SWD (Debug Probe or picoprobe).
# Rescue reset first: with core 1 running from flash, erasing the 864 KB image (M6 RAM disk)
# timed out ("failed erasing sectors"); after a rescue reset it programs cleanly (2026-10-12).
flash: all
	$(OPENOCD) -f interface/cmsis-dap.cfg -c "set RESCUE 1" -c "set USE_CORE 0" -f target/rp2350.cfg -c "adapter speed 5000" -c "init; exit"
	$(OPENOCD) $(OCD_CFG) -c "program $(ELF) verify reset exit"

reset:
	$(OPENOCD) $(OCD_CFG) -c "init; reset run; exit"

openocd:
	$(OPENOCD) $(OCD_CFG)

gdb: all
	$(GDB) $(ELF) -ex "target extended-remote :3333" -ex "monitor reset halt"

clean:
	rm -rf $(BUILD)
	$(MAKE) -C test clean

.PHONY: all test flash reset openocd gdb clean
