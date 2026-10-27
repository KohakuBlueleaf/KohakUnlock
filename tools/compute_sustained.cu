// SPDX-License-Identifier: GPL-2.0-only
// Sustained cuBLAS GEMM verification; separate methodology from compute_smoke.cu.
#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cublas_v2.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

static void check(cudaError_t s) {
    if (s != cudaSuccess) throw std::runtime_error(cudaGetErrorString(s));
}
static void check(cublasStatus_t s) {
    if (s != CUBLAS_STATUS_SUCCESS)
        throw std::runtime_error("cuBLAS status " + std::to_string(s));
}
struct Buffer {
    void *p = nullptr;
    explicit Buffer(size_t bytes) { check(cudaMalloc(&p, bytes)); }
    ~Buffer() { cudaFree(p); }
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
};
struct Event {
    cudaEvent_t e;
    Event() { check(cudaEventCreate(&e)); }
    ~Event() { cudaEventDestroy(e); }
};
struct Blas {
    cublasHandle_t h;
    Blas() { check(cublasCreate(&h)); }
    ~Blas() { cublasDestroy(h); }
};

template<class Input, class Output, class Scalar>
static void run(cublasHandle_t h, int n, double seconds, const char *label,
                cudaDataType input_type, cudaDataType output_type,
                cublasComputeType_t compute, Scalar one, Scalar zero,
                bool transpose_a = false) {
    const size_t count = size_t(n) * n;
    std::vector<Input> input(count);
    for (int col = 0; col < n; ++col)
        for (int row = 0; row < n; ++row)
            input[row + size_t(col) * n] = Input(((row + col) & 1) ? -1.f : 1.f);
    Buffer a(count * sizeof(Input)), b(count * sizeof(Input)), c(count * sizeof(Output));
    check(cudaMemcpy(a.p, input.data(), count * sizeof(Input), cudaMemcpyHostToDevice));
    check(cudaMemcpy(b.p, input.data(), count * sizeof(Input), cudaMemcpyHostToDevice));
    check(cudaMemset(c.p, 0, count * sizeof(Output)));
    auto gemm = [&]() {
        check(cublasGemmEx(h, transpose_a ? CUBLAS_OP_T : CUBLAS_OP_N, CUBLAS_OP_N, n, n, n,
              &one, a.p, input_type, n, b.p, input_type, n, &zero,
              c.p, output_type, n, compute, CUBLAS_GEMM_DEFAULT_TENSOR_OP));
    };
    for (int i = 0; i < 10; ++i) gemm();
    check(cudaDeviceSynchronize());
    Event start, stop;
    constexpr unsigned batch = 32;
    unsigned long long iterations = 0;
    double total_ms = 0, min_ms = 1e30, max_ms = 0;
    const auto wall_start = std::chrono::steady_clock::now();
    std::printf("BEGIN %s n=%d target_GPU_seconds=%.3f warmup=10 batch=%u layout=%s\n",
                label, n, seconds, batch, transpose_a ? "TN" : "NN");
    std::fflush(stdout);
    while (total_ms < seconds * 1000) {
        check(cudaEventRecord(start.e));
        for (unsigned i = 0; i < batch; ++i) gemm();
        check(cudaEventRecord(stop.e));
        check(cudaEventSynchronize(stop.e));
        float ms;
        check(cudaEventElapsedTime(&ms, start.e, stop.e));
        if (!(ms > 0) || !std::isfinite(ms)) throw std::runtime_error("invalid duration");
        total_ms += ms;
        iterations += batch;
        min_ms = std::fmin(min_ms, ms / batch);
        max_ms = std::fmax(max_ms, ms / batch);
    }
    const double wall_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - wall_start).count();
    std::vector<Output> result(count);
    check(cudaMemcpy(result.data(), c.p, count * sizeof(Output), cudaMemcpyDeviceToHost));
    // Check the final complete output, not every timed iteration.
    // Structured +/-1 inputs have the exactly representable analytical result +/-n.
    for (int col = 0; col < n; ++col)
        for (int row = 0; row < n; ++row) {
            const float expected = ((row + col) & 1) ? -float(n) : float(n);
            if (float(result[row + size_t(col) * n]) != expected)
                throw std::runtime_error(std::string(label) + " incorrect output row=" +
                                         std::to_string(row) + " col=" + std::to_string(col));
        }
    const double ms = total_ms / iterations;
    const double tops = 2.0 * n * n * n / (ms * 1e9);
    const char *unit = input_type == CUDA_R_8I ? "TOPs" : "TFLOPs";
    std::printf("RESULT %s iterations=%llu GPU_seconds=%.6f wall_seconds=%.6f "
                "ms_per_GEMM=%.6f %s=%.3f batch_ms_per_GEMM_min=%.6f "
                "batch_ms_per_GEMM_max=%.6f correctness=PASS outputs=%zu\n",
                label, iterations, total_ms / 1000, wall_seconds, ms, unit, tops,
                min_ms, max_ms, count);
    std::fflush(stdout);
}

int main(int argc, char **argv) {
    try {
        if (argc != 4 && argc != 5)
            throw std::runtime_error("Usage: compute_sustained PCI_BDF seconds N [fp16|pure-fp16|int8|int8-tn|all]");
        const double seconds = std::stod(argv[2]);
        const int n = std::stoi(argv[3]);
        const std::string mode = argc == 5 ? argv[4] : "fp16";
        if (mode != "fp16" && mode != "pure-fp16" && mode != "int8" && mode != "int8-tn" && mode != "all")
            throw std::runtime_error("unknown accumulation mode");
        if (!std::isfinite(seconds) || seconds < 1 || seconds > 60 ||
            n < 2048 || n > 16384 || n % 256)
            throw std::runtime_error("seconds must be 1..60, N a multiple of 256 in 2048..16384");
        int device, driver, runtime, blas_version;
        check(cudaDeviceGetByPCIBusId(&device, argv[1]));
        check(cudaSetDevice(device));
        cudaDeviceProp prop;
        check(cudaGetDeviceProperties(&prop, device));
        if (prop.major != 7 || prop.minor != 5)
            throw std::runtime_error("this verification is scoped to SM 7.5");
        char bdf[32];
        check(cudaDeviceGetPCIBusId(bdf, sizeof(bdf), device));
        int confirmed;
        check(cudaDeviceGetByPCIBusId(&confirmed, bdf));
        if (confirmed != device) throw std::runtime_error("device identity mismatch");
        check(cudaDriverGetVersion(&driver));
        check(cudaRuntimeGetVersion(&runtime));
        Blas blas;
        check(cublasGetVersion(blas.h, &blas_version));
        check(cublasSetMathMode(blas.h, CUBLAS_TENSOR_OP_MATH));
        std::printf("GPU=%s BDF=%s SM=%d.%d multiprocessors=%d CUDA_driver=%d "
                    "CUDA_runtime=%d cuBLAS=%d\n", prop.name, bdf, prop.major,
                    prop.minor, prop.multiProcessorCount, driver, runtime, blas_version);
        std::puts("Column-major N^3 GEMM alpha=1 beta=0; CUDA-event sum, 2*N^3 ops/GEMM; "
                  "transfers and validation excluded. Each mode runs for the requested duration.");
        if (mode == "fp16" || mode == "all")
            run<__half, float>(blas.h, n, seconds, "FP16_to_FP32", CUDA_R_16F, CUDA_R_32F,
                              CUBLAS_COMPUTE_32F, 1.f, 0.f);
        if (mode == "fp16" || mode == "pure-fp16" || mode == "all")
            run<__half, __half>(blas.h, n, seconds, "FP16_to_FP16", CUDA_R_16F, CUDA_R_16F,
                               CUBLAS_COMPUTE_16F, __float2half(1.f), __float2half(0.f));
        if (mode == "int8" || mode == "all")
            run<int8_t, int32_t>(blas.h, n, seconds, "INT8_to_INT32", CUDA_R_8I, CUDA_R_32I,
                                CUBLAS_COMPUTE_32I, int32_t(1), int32_t(0));
        // Ordinary column-major IMMA requires TN. Keep NN as a separate baseline.
        // This structured input is symmetric, so op(A)=A^T has the same exact result.
        if (mode == "int8-tn" || mode == "all")
            run<int8_t, int32_t>(blas.h, n, seconds, "INT8_TN_to_INT32", CUDA_R_8I, CUDA_R_32I,
                                CUBLAS_COMPUTE_32I, int32_t(1), int32_t(0), true);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
