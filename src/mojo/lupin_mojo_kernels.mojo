from std.math import sqrt, exp

# 🐺 LupinOS Mojo AI Compute Kernel Suite
# Target Architecture: AArch64 Cortex-X1 (Snapdragon 888 / SM8350)
# C-ABI Exported Functions for Mobile Silicon Acceleration

@export("mojo_dot_f32")
def mojo_dot_f32(a_addr: Int, b_addr: Int, n: Int32) abi("c") -> Float32:
    var a = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=a_addr)
    var b = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=b_addr)
    var acc: Float32 = 0.0
    for i in range(Int(n)):
        acc += a[unsafe_offset=i] * b[unsafe_offset=i]
    return acc

@export("mojo_gemm_f32")
def mojo_gemm_f32(a_addr: Int, b_addr: Int, c_addr: Int, M: Int32, N: Int32, K: Int32) abi("c") -> None:
    var A = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=a_addr)
    var B = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=b_addr)
    var C = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=c_addr)
    
    var m_val = Int(M)
    var n_val = Int(N)
    var k_val = Int(K)
    
    for i in range(m_val):
        var a_row = i * k_val
        var c_row = i * n_val
        for j in range(n_val):
            var sum: Float32 = 0.0
            for k in range(k_val):
                sum += A[unsafe_offset=a_row + k] * B[unsafe_offset=k * n_val + j]
            C[unsafe_offset=c_row + j] = sum

@export("mojo_rmsnorm_f32")
def mojo_rmsnorm_f32(x_addr: Int, w_addr: Int, out_addr: Int, n: Int32, eps: Float32) abi("c") -> None:
    var x = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=x_addr)
    var w = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=w_addr)
    var out = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=out_addr)
    
    var count = Int(n)
    var sum_sq: Float32 = 0.0
    for i in range(count):
        var val = x[unsafe_offset=i]
        sum_sq += val * val
        
    var rms = 1.0 / sqrt((sum_sq / Float32(count)) + eps)
    for i in range(count):
        out[unsafe_offset=i] = x[unsafe_offset=i] * rms * w[unsafe_offset=i]

@export("mojo_silu_hadamard_f32")
def mojo_silu_hadamard_f32(gate_addr: Int, up_addr: Int, out_addr: Int, n: Int32) abi("c") -> None:
    var gate = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=gate_addr)
    var up = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=up_addr)
    var out = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=out_addr)
    
    var count = Int(n)
    for i in range(count):
        var g = gate[unsafe_offset=i]
        var u = up[unsafe_offset=i]
        var sigmoid = 1.0 / (1.0 + exp(-g))
        var silu = g * sigmoid
        out[unsafe_offset=i] = silu * u
