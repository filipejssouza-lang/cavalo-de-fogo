CC ?= gcc
ARCH_FLAGS ?= -march=armv8.2-a+dotprod
CFLAGS = -O3 -static $(ARCH_FLAGS) -ffast-math -Wall
LDFLAGS = -lm -lpthread -ldl

SRC_CPU = src/cpu/neon_kernels.c src/cpu/governor_control.c
SRC_NPU = src/npu/fastrpc_cdsp.c src/npu/fastrpc_npu.c
SRC_GUARDIAN = src/guardian/lupinos_guard.c src/guardian/net_watchdog.c
SRC_BENCH = src/bench/bench_extreme.c

OBJ_CPU = $(SRC_CPU:.c=.o)
OBJ_NPU = $(SRC_NPU:.c=.o)
OBJ_GUARDIAN = $(SRC_GUARDIAN:.c=.o)
OBJ_BENCH = $(SRC_BENCH:.c=.o)

TARGETS = build/lupinos_guard build/bench_extreme build/drm_subdisplay build/lupin-power

all: $(TARGETS)


hud: build/drm_subdisplay

build/drm_subdisplay: src/display/drm_subdisplay.c
	@mkdir -p build
	$(CC) -O2 -static -Wall $< -o $@ $(LDFLAGS)

build/lupin-power: src/cpu/lupin_power.c src/cpu/governor_control.c
	@mkdir -p build
	$(CC) -O2 -static -Wall $^ -o $@ $(LDFLAGS)

build/lupinos_guard: $(OBJ_GUARDIAN) $(OBJ_NPU)
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

.PHONY: all clean hud
