CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra

all: bench_mojo libmojokernels.so

libmojokernels.so: lupin_mojo_cortex_x1.o
	$(CC) -shared -O3 -o $@ $^ -lm

bench_mojo: bench_mojo.c lupin_mojo_cortex_x1.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

clean:
	rm -f bench_mojo libmojokernels.so
