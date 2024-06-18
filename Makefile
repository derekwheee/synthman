# Project Name
TARGET = synthman

USE_DAISYSP_LGPL = 1

# Sources
CPP_SOURCES += synthman.cpp
CPP_SOURCES += synthvoice.cpp

# Library Locations
LIBDAISY_DIR = ../DaisyExamples/libDaisy/
DAISYSP_DIR = ../DaisyExamples/DaisySP/

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
