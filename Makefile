CC ?= gcc
CFLAGS = -O3 -static -mcpu=cortex-x1+dotprod -ffast-math -Wall
LDFLAGS = -lm -lpthread -ldl

SRC_CPU = src/cpu/neon_kernels.c
SRC_NPU = src/npu/fastrpc_cdsp.c
SRC_GUARDIAN = src/guardian/lupinos_guard.c
SRC_BENCH = src/bench/bench_extreme.c

OBJ_CPU = $(SRC_CPU:.c=.o)
OBJ_NPU = $(SRC_NPU:.c=.o)
OBJ_GUARDIAN = $(SRC_GUARDIAN:.c=.o)
OBJ_BENCH = $(SRC_BENCH:.c=.o)

TARGETS = build/lupinos_guard build/bench_extreme

all: $(TARGETS)

build/lupinos_guard: $(OBJ_GUARDIAN)
	@mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

build/bench_extreme: $(OBJ_BENCH) $(OBJ_CPU) $(OBJ_NPU)
	@mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build
	rm -f $(OBJ_CPU) $(OBJ_NPU) $(OBJ_GUARDIAN) $(OBJ_BENCH)

.PHONY: all clean
