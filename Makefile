# ==============================================================================
# Makefile for ant_server (C++20 io_uring Web Server)
# ==============================================================================

CXX ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -luring

BUILD_DIR ?= build
BIN_TARGET ?= $(BUILD_DIR)/ant_server_test

# Find all C++ source and header files for formatting
SRC_FILES := $(shell find include test -type f \( -name "*.hpp" -o -name "*.cpp" -o -name "*.h" -o -name "*.c" \))

.PHONY: all build run format format-check clean help

all: build

build:
	@mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake .. && $(MAKE) -j$(nproc)
	@echo "Build successful: $(BIN_TARGET)"

run: build
	./$(BIN_TARGET)

format:
	@echo "Formatting C++ files with clang-format..."
	@clang-format -i --style=file $(SRC_FILES)
	@echo "Formatting complete!"

format-check:
	@echo "Checking C++ code format with clang-format..."
	@clang-format --dry-run --Werror --style=file $(SRC_FILES)
	@echo "All files conform to formatting standards."

clean:
	@rm -rf $(BUILD_DIR) /tmp/ant_server_test
	@echo "Cleaned build artifacts."

help:
	@echo "Available targets:"
	@echo "  make format       - Format all .hpp and .cpp files using clang-format"
	@echo "  make format-check - Check formatting compliance without modifying files"
	@echo "  make build        - Compile the ant_server test binary"
	@echo "  make run          - Compile and run the ant_server test binary"
	@echo "  make clean        - Remove build directory"
