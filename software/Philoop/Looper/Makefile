# Project Name
TARGET = looper

USE_DAISYSP_LGPL = 1

APP_TYPE = BOOT_SRAM

OPT = -Os

CPP_SOURCES = looper.cpp looper_granular_player.cpp

LIBDAISY_DIR = ../../../libDaisy
DAISYSP_DIR = ../../../DaisySP

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

C_INCLUDES += -I../../../include
