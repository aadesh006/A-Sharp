CC = gcc
CFLAGS = -g -Wall
LIBS = -lreadline -lm

# LLVM compiler and flags (uses llvm-config to auto-detect installation)
# -DUSE_LLVM tells the preprocessor to compile in the LLVM backend code
LLVM_CC = clang
LLVM_CFLAGS = $(shell llvm-config --cflags) -g -Wall -DUSE_LLVM
LLVM_LIBS = $(shell llvm-config --ldflags --libs core executionengine mcjit native orcjit) -lreadline -lm

BUILD_DIR = build
LLVM_BUILD_DIR = build_llvm

# All .c files EXCEPT llvm_backend.c (that's only for the LLVM build)
SOURCES = $(filter-out llvm_backend.c, $(wildcard *.c))
OBJECTS = $(SOURCES:%.c=$(BUILD_DIR)/%.o)

# For the LLVM build: all .c files INCLUDING llvm_backend.c
LLVM_SOURCES = $(wildcard *.c)
LLVM_OBJECTS = $(LLVM_SOURCES:%.c=$(LLVM_BUILD_DIR)/%.o)

TARGET = asharp
LLVM_TARGET = asharp_llvm

# Default build — existing VM, no LLVM
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# LLVM build — compiles everything including llvm_backend.c
llvm: $(LLVM_TARGET)

$(LLVM_TARGET): $(LLVM_OBJECTS)
	$(LLVM_CC) $(LLVM_CFLAGS) -o $@ $^ $(LLVM_LIBS)

$(LLVM_BUILD_DIR)/%.o: %.c | $(LLVM_BUILD_DIR)
	$(LLVM_CC) $(LLVM_CFLAGS) -c $< -o $@

$(LLVM_BUILD_DIR):
	mkdir -p $(LLVM_BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(LLVM_BUILD_DIR) $(TARGET) $(LLVM_TARGET)

.PHONY: all llvm clean
