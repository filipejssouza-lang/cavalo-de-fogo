CXX ?= g++
CXXFLAGS = -O3 -Wall -Wextra -std=c++17
LDFLAGS = -lvulkan

TARGETS = bench_turnip_uma vulkan_adreno_gemm gemm.spv

all: $(TARGETS)

gemm.spv: gemm.comp
	glslc gemm.comp -o gemm.spv

bench_turnip_uma: bench_turnip_uma.cpp uma_allocator.cpp
	$(CXX) $(CXXFLAGS) bench_turnip_uma.cpp -o bench_turnip_uma $(LDFLAGS)

vulkan_adreno_gemm: vulkan_adreno_gemm.cpp gemm.spv
	$(CXX) $(CXXFLAGS) vulkan_adreno_gemm.cpp -o vulkan_adreno_gemm $(LDFLAGS)

clean:
	rm -f bench_turnip_uma vulkan_adreno_gemm gemm.spv
