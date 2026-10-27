// SPDX-License-Identifier: GPL-2.0-only
// Small CUDA/cuBLAS GEMM baseline; no firmware, BAR or binding operations.
#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cublas_v2.h>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

static void cuda_check(cudaError_t status)
{
    if (status != cudaSuccess) throw std::runtime_error(cudaGetErrorString(status));
}
static void blas_check(cublasStatus_t status)
{
    if (status != CUBLAS_STATUS_SUCCESS)
        throw std::runtime_error("cuBLAS status " + std::to_string(status));
}
struct Buffer {
    void *ptr = nullptr;
    explicit Buffer(size_t bytes) { cuda_check(cudaMalloc(&ptr, bytes)); }
    ~Buffer() { cudaFree(ptr); }
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
};
struct Event {
    cudaEvent_t value;
    Event() { cuda_check(cudaEventCreate(&value)); }
    ~Event() { cudaEventDestroy(value); }
};
struct Blas {
    cublasHandle_t value;
    Blas() { blas_check(cublasCreate(&value)); }
    ~Blas() { cublasDestroy(value); }
};

template<class Input, class Output, class Scalar>
static void measure(cublasHandle_t handle, const char *label,
                    cudaDataType input_type, cudaDataType output_type,
                    cublasComputeType_t compute_type, Scalar one, Scalar zero)
{
    constexpr int n = 2048, warmup = 3, iterations = 20;
    const size_t count = size_t(n) * n;
    std::vector<Input> host(count);
    // Column-major A(r,k)=(-1)^(r+k), B(k,c)=(-1)^(k+c).
    // Exact result C(r,c)=n*(-1)^(r+c), representable in all output types.
    for (int col = 0; col < n; ++col)
        for (int row = 0; row < n; ++row)
            host[row + size_t(col) * n] = Input(((row + col) & 1) ? -1 : 1);
    Buffer a(count * sizeof(Input)), b(count * sizeof(Input));
    Buffer c(count * sizeof(Output));
    cuda_check(cudaMemcpy(a.ptr, host.data(), count * sizeof(Input), cudaMemcpyHostToDevice));
    cuda_check(cudaMemcpy(b.ptr, host.data(), count * sizeof(Input), cudaMemcpyHostToDevice));
    cuda_check(cudaMemset(c.ptr, 0, count * sizeof(Output)));
    auto gemm = [&]() {
        blas_check(cublasGemmEx(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n,
            &one, a.ptr, input_type, n, b.ptr, input_type, n,
            &zero, c.ptr, output_type, n, compute_type, CUBLAS_GEMM_DEFAULT));
    };
    for (int i = 0; i < warmup; ++i) gemm();
    cuda_check(cudaDeviceSynchronize());
    Event start, stop;
    cuda_check(cudaEventRecord(start.value));
    for (int i = 0; i < iterations; ++i) gemm();
    cuda_check(cudaEventRecord(stop.value));
    cuda_check(cudaEventSynchronize(stop.value));
    float elapsed;
    cuda_check(cudaEventElapsedTime(&elapsed, start.value, stop.value));
    if (!(elapsed > 0)) throw std::runtime_error("invalid event duration");
    std::vector<Output> result(count);
    cuda_check(cudaMemcpy(result.data(), c.ptr, count * sizeof(Output), cudaMemcpyDeviceToHost));
    for (int col = 0; col < n; ++col)
        for (int row = 0; row < n; ++row) {
            const float expected = ((row + col) & 1) ? -n : n;
            if (float(result[row + size_t(col) * n]) != expected)
                throw std::runtime_error(std::string(label) + " incorrect result at row=" +
                    std::to_string(row) + " col=" + std::to_string(col));
        }
    const double ms = elapsed / iterations;
    const double tops = 2.0 * n * n * n / (ms * 1e9);
    std::printf("%-16s %9.4f ms/GEMM  %8.3f %s  correctness=PASS (all %zu outputs)\n",
        label, ms, tops, input_type == CUDA_R_8I ? "TOPS   " : "TFLOP/s", count);
    std::fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s PCI_BDF  (example: 0000:01:00.0)\n", argv[0]);
        return 2;
    }
    try {
        int device, driver, runtime, blas_version;
        cuda_check(cudaDeviceGetByPCIBusId(&device, argv[1]));
        cuda_check(cudaSetDevice(device));
        cudaDeviceProp prop;
        cuda_check(cudaGetDeviceProperties(&prop, device));
        char bdf[32];
        cuda_check(cudaDeviceGetPCIBusId(bdf, sizeof(bdf), device));
        int confirmed;
        cuda_check(cudaDeviceGetByPCIBusId(&confirmed, bdf));
        if (confirmed != device) throw std::runtime_error("device identity mismatch");
        cuda_check(cudaDriverGetVersion(&driver));
        cuda_check(cudaRuntimeGetVersion(&runtime));
        Blas handle;
        blas_check(cublasGetVersion(handle.value, &blas_version));
        std::printf("GPU=%s BDF=%s SM=%d.%d VRAM_MiB=%zu\n",
            prop.name, bdf, prop.major, prop.minor, prop.totalGlobalMem / (1024 * 1024));
        std::printf("CUDA_driver=%d CUDA_runtime=%d cuBLAS=%d\n", driver, runtime, blas_version);
        std::puts("GEMM: M=N=K=2048, column-major, alpha=1 beta=0, warmup=3 timed=20");
        std::puts("CUDA events; transfers/validation excluded; multiply+add counts as 2 ops.");
        std::puts("cuBLAS throughput, not instruction peak or long-term stability evidence.");
        std::fflush(stdout);
        measure<float, float>(handle.value, "FP32 -> FP32", CUDA_R_32F, CUDA_R_32F,
            CUBLAS_COMPUTE_32F_PEDANTIC, 1.0f, 0.0f);
        measure<__half, float>(handle.value, "FP16 -> FP32", CUDA_R_16F, CUDA_R_32F,
            CUBLAS_COMPUTE_32F, 1.0f, 0.0f);
        measure<__half, __half>(handle.value, "FP16 -> FP16", CUDA_R_16F, CUDA_R_16F,
            CUBLAS_COMPUTE_16F, __float2half(1.0f), __float2half(0.0f));
        measure<int8_t, int32_t>(handle.value, "INT8 -> INT32", CUDA_R_8I, CUDA_R_32I,
            CUBLAS_COMPUTE_32I, int32_t(1), int32_t(0));
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
