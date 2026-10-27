cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico2)
include(pico_sdk_import.cmake)
project(a2m_bringup C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(a2m_bringup
    src/main.c src/hw.c src/crc32.c src/mech.c src/mech_addr.c src/console.c src/flux.c
    src/gcr.c src/flux_dec.c src/trackbuf.c src/ipc.c src/mech_server.c src/wtrack.c src/flux_tx.c
    src/sp_wire.c src/sp_dev.c src/ramdisk.c src/sp_bus.c src/sp_hw.c src/sp_image.S src/fstore.c src/loop.c)
pico_generate_pio_header(a2m_bringup ${CMAKE_CURRENT_LIST_DIR}/src/flux_rx.pio)
pico_generate_pio_header(a2m_bringup ${CMAKE_CURRENT_LIST_DIR}/src/flux_tx.pio)
pico_generate_pio_header(a2m_bringup ${CMAKE_CURRENT_LIST_DIR}/src/sp_fm.pio)
pico_generate_pio_header(a2m_bringup ${CMAKE_CURRENT_LIST_DIR}/src/sp_chain.pio)

# Optional 800K ProDOS image linked into flash for the RAM disk (spec sec. 6.2). Not in the
# repo (Apple software): build one with tools/apple/mkimage.sh or pass -DA2M_DISK_IMAGE=<file>.po.
# Without it the RAM disk reads as zeros.
set(A2M_DISK_IMAGE "${CMAKE_CURRENT_LIST_DIR}/../docs/private/ref/m6_boot800k.po" CACHE FILEPATH
    "800K ProDOS image linked into flash for the M6 RAM disk")
if(EXISTS "${A2M_DISK_IMAGE}")
    set_source_files_properties(src/sp_image.S PROPERTIES
        COMPILE_DEFINITIONS "A2M_DISK_IMAGE_PATH=\"${A2M_DISK_IMAGE}\""
        OBJECT_DEPENDS "${A2M_DISK_IMAGE}")
    message(STATUS "M6 RAM disk image: ${A2M_DISK_IMAGE}")
else()
    message(WARNING "M6 RAM disk image not found (${A2M_DISK_IMAGE}): the disk reads as zeros")
endif()
target_include_directories(a2m_bringup PRIVATE src)
target_compile_options(a2m_bringup PRIVATE -Wall -Wextra)
# core 1: gcr_encode_data alone uses ~1.25 KB of stack. SCRATCH_X is 4 KB, so 0x1000 is the maximum; 0x2000 does not link.
target_compile_definitions(a2m_bringup PRIVATE PICO_CORE1_STACK_SIZE=0x1000)
target_link_libraries(a2m_bringup pico_stdlib pico_multicore hardware_pio hardware_dma)
pico_enable_stdio_usb(a2m_bringup 1)
pico_enable_stdio_uart(a2m_bringup 0)
pico_add_extra_outputs(a2m_bringup)
