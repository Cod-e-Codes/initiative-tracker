# D&D Initiative Tracker Makefile

CC = gcc
CFLAGS = -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror -std=c11
LDFLAGS = -lncurses
TARGET = initiative
SOURCE = initiative.c

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) $(LDFLAGS) -o $(TARGET)

# Clean build artifacts
clean:
	rm -f $(TARGET) $(TARGET).exe *.o

# Install (optional - copies to /usr/local/bin)
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# Uninstall
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Debug build target
debug: CFLAGS = -Wall -Wextra -g -O0 -std=c11
debug: $(TARGET)

# Phony targets
.PHONY: all clean install uninstall debug

