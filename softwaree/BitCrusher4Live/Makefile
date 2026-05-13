# Project Name
TARGET = BitCrush4Live

USE_DAISYSP_LGPL = 1

APP_TYPE = BOOT_SRAM

# Compiler options
OPT = -Os

# Sources
CPP_SOURCES = BitCrush.cpp \
              moogladder.cpp \
              oscillator.cpp \
              sampleratereducer.cpp \
              svf.cpp

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

C_INCLUDES += -I. -I../../include -I../../DaisySP/Source/Utility
