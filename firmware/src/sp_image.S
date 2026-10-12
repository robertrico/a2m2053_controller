// M6 RAM disk base image (spec sec. 6.2), linked into flash. The image itself is never in git
// (it holds Apple's ProDOS): CMake passes its path when the file exists.
    .section .rodata.sp_image, "a"
    .balign 4
    .global sp_image
    .global sp_image_end
sp_image:
#ifdef A2M_DISK_IMAGE_PATH
    .incbin A2M_DISK_IMAGE_PATH
#endif
sp_image_end:
