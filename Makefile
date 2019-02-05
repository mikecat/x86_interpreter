CC=gcc
CFLAGS=-O2 -Wall -Wextra

TARGET=x86_interpreter

OBJS=x86_interpreter.o dynamic_memory.o \
	read_file.o read_raw.o read_elf.o \
	xv6_syscall.o

$(TARGET): $(OBJS)
	$(CC) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)
