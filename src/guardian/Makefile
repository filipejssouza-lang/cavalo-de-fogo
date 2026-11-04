CC ?= aarch64-linux-gnu-gcc
CFLAGS = -static -O2 -s -Wall -Wextra

all: lupinos_guard

lupinos_guard: lupinos_guard.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f lupinos_guard