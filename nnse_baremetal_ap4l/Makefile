# Simple Makefile for nnse_baremetal demo

# Compiler and tools
CC = gcc
AR = ar
OBJCOPY = objcopy

# Directories
SRC_DIR = src
BUILD_DIR = build
LIB_DIR = libs

# AmbiqSuite version and paths
AS_VERSION = R5.3.0
AMBIQ_ROOT = ../../../extern/AmbiqSuite/$(AS_VERSION)

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Target
TARGET = nnse_baremetal

# Include paths for AmbiqSuite
INCLUDES = -I$(SRC_DIR) \
           -I$(AMBIQ_ROOT)/boards/apollo510_evb/bsp \
           -I$(AMBIQ_ROOT)/src \
           -I$(AMBIQ_ROOT)/CMSIS/AmbiqMicro/Include \
           -I$(AMBIQ_ROOT)/devices \
           -I$(AMBIQ_ROOT)/mcu/apollo510 \
           -I$(AMBIQ_ROOT)/mcu/apollo510/hal \
           -I$(AMBIQ_ROOT)/mcu/apollo510/hal/mcu \
           -I$(AMBIQ_ROOT)/utils

# Compiler flags
CFLAGS = -Wall -O2 $(INCLUDES)

# Default target
all: $(BUILD_DIR) $(TARGET)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build the target
$(TARGET): $(OBJECTS) $(LIB_DIR)/ns-nnsp.a
	$(CC) $(OBJECTS) -L$(LIB_DIR) -lnnsp -lm -o $(BUILD_DIR)/$@

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

# Help target
help:
	@echo "Available targets:"
	@echo "  all     - Build the project"
	@echo "  clean   - Remove build artifacts"
	@echo "  help    - Show this help message"

.PHONY: all clean help