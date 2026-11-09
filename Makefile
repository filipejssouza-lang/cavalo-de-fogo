CC ?= gcc
ARCH_Check := $(shell $(CC) -dumpmachine 2>/dev/null)

ifneq (,$(findstring aarch64,$(ARCH_Check)))
    ARCH_FLAGS = -march=armv8.4-a+dotprod
    STATIC ?= -static
else
    ARCH_FLAGS =
    STATIC ?=
endif

CFLAGS = -O3 $(STATIC) $(ARCH_FLAGS) -ffast-math -Wall
LDFLAGS = $(STATIC) -lm -lpthread -ldl

SRC_CPU = src/cpu/neon_kernels.c src/cpu/governor_control.c
SRC_NPU = src/npu/fastrpc_cdsp.c src/npu/fastrpc_npu.c
SRC_GUARDIAN = src/guardian/lupinos_guard.c src/guardian/net_watchdog.c src/cpu/governor_daemon.c src/cpu/governor_control.c
SRC_BENCH = src/bench/bench_extreme.c

OBJ_CPU = $(SRC_CPU:.c=.o)
OBJ_NPU = $(SRC_NPU:.c=.o)
OBJ_GUARDIAN = $(SRC_GUARDIAN:.c=.o)
OBJ_BENCH = $(SRC_BENCH:.c=.o)

TARGETS = build/lupinos_guard build/bench_extreme build/drm_subdisplay build/lupin-power build/lupinctl build/lupin_slot_ctl build/lupin_lp_query build/lupin-audio

all: $(TARGETS)


hud: build/drm_subdisplay

build/drm_subdisplay: src/display/drm_subdisplay.c
	@mkdir -p build
	$(CC) -O2 $(STATIC) -Wall $< -o $@ $(LDFLAGS)

build/lupin-power: src/cpu/lupin_power.c src/cpu/governor_control.c
	@mkdir -p build
	$(CC) -O2 $(STATIC) -Wall $^ -o $@ $(LDFLAGS)

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

build/lupinctl: src/lupinctl/lupinctl.c
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

lupinctl: build/lupinctl

build/lupin_slot_ctl: tools/lupin_slot_ctl.c
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

slot_ctl: build/lupin_slot_ctl

build/lupin_lp_query: tools/lupin_lp_query.c
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

build/lupin-audio: src/audio/lupin_audio_play.c src/audio/lupin_audio.c
	@mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

build/test_audio: tests/test_audio.c src/audio/lupin_audio.c
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc/audio $^ -o $@ $(LDFLAGS)


