LIB := lib

INCLUDE_DIRS += -I $(LIB)

USE_SDMMC = $(shell grep -E "^\#define CONFIG_BOOT_(SDCARD|MMC)" board.h)

ifneq ($(USE_SDMMC),)
SRCS	+=  $(LIB)/loaders.c
endif

SRCS	+=  $(LIB)/fdt.c
SRCS	+=  $(LIB)/bootstate.c
SRCS	+=  $(LIB)/bootstate_storage.c
SRCS	+=  $(LIB)/rauc_mbr.c
SRCS	+=  $(LIB)/rauc_boot.c
SRCS	+=  $(LIB)/debug.c
SRCS	+=  $(LIB)/string.c
SRCS	+=  $(LIB)/xformat.c

include lib/fatfs/fatfs.mk
