CC=gcc
CFLAGS=-O2

TARGET=x86_interpreter

OBJS=x86_interpreter.o dynamic_memory.o read_raw.o

$(TARGET): $(OBJS)
	$(CC) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)
