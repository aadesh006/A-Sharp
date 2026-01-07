CC = gcc
CFLAGS = -g -Wall
LIBS = -lreadline

# The directory where object files will go
BUILD_DIR = build

SOURCES = $(wildcard *.c)
# This changes 'main.c' -> 'build/main.o'
OBJECTS = $(SOURCES:%.c=$(BUILD_DIR)/%.o)

TARGET = asharp

all: $(TARGET)

# Link: Now looks for objects inside the build folder
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Compile: Puts .o files into the build folder
# The '| $(BUILD_DIR)' part ensures the folder exists first
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Create the build directory if it doesn't exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean