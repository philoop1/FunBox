# Project Name
TARGET = BitCrush4Live2

USE_DAISYSP_LGPL = 1

APP_TYPE = BOOT_SRAM

# Compiler options
OPT = -Os

# Sources
CPP_SOURCES = BitCrush.cpp \
              funbox_phaser.cpp \
              funbox_flanger.cpp \
              funbox_wavefolder.cpp \
              moogladder.cpp \
              oscillator.cpp \
              sampleratereducer.cpp \
              svf.cpp

# Library Locations
LIBDAISY_DIR = ../../../libDaisy
DAISYSP_DIR = ../../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

C_INCLUDES += -I. -I../../../include -I$(DAISYSP_DIR)/Source -I$(DAISYSP_DIR)/Source/Utility
