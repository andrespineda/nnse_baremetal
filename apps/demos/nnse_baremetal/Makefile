# Simple Makefile for nnse_baremetal demo

# Compiler and tools
CC = gcc
AR = ar
OBJCOPY = objcopy

# Directories
SRC_DIR = src
BUILD_DIR = build
LIB_DIR = libs

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Target
TARGET = nnse_baremetal

# Compiler flags
CFLAGS = -Wall -O2 -I$(SRC_DIR)

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