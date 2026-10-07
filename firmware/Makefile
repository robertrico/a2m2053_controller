PICO_SDK_PATH ?= $(HOME)/Development/pico-sdk
export PICO_SDK_PATH
BUILD   := build
ELF     := $(BUILD)/a2m_bringup.elf

# SWD via picoprobe / Debug Probe (CMSIS-DAP). Default: Raspberry Pi's prebuilt
# OpenOCD (pico-sdk-tools v2.3.1-0, has target/rp2350.cfg). Override with OPENOCD=.
OPENOCD ?= $(HOME)/Development/tooling/openocd-rpi/current/openocd
OCD_CFG := -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000"
GDB     ?= arm-none-eabi-gdb

all: $(BUILD)/build.ninja
	cmake --build $(BUILD)

$(BUILD)/build.ninja:
	cmake -B $(BUILD) -S . -G Ninja

test:
	$(MAKE) -C test

# Rico only (hardware actions):
flash: all
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
