# Makefile for the LSH shell program

CC = gcc
CFLAGS = -Wall -Wextra -g

# Source files
SRCS = main.c shell.c builtins.c line_reader.c tab_complete.c

# Object files
OBJS = $(SRCS:.c=.o)

# Header files
HEADERS = common.h shell.h builtins.h line_reader.h tab_complete.h

# Executable name
TARGET = lsh

# Default target
all: $(TARGET)

# Link the program
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compile source files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets
.PHONY: all clean
