// FP16-input GEMM throughput bench. Two paths:
//   FP32-accum (CUBLAS_COMPUTE_32F): the GeForce-capped tensor path.
//   FP16-accum (CUBLAS_COMPUTE_16F): control, not capped.
// Reports TFLOPS per device.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cublas_v2.h>

#define CK(x) do{ cudaError_t e=(x); if(e!=cudaSuccess){printf("cuda err %s:%d %s\n",__FILE__,__LINE__,cudaGetErrorString(e));exit(1);} }while(0)
#define CB(x) do{ cublasStatus_t s=(x); if(s!=CUBLAS_STATUS_SUCCESS){printf("cublas err %s:%d %d\n",__FILE__,__LINE__,(int)s);exit(1);} }while(0)

static double run(cublasHandle_t h, int N, cublasComputeType_t ct, int iters){
  size_t n = (size_t)N*N;
  half *A,*B; float *C;
  CK(cudaMalloc(&A, n*sizeof(half)));
  CK(cudaMalloc(&B, n*sizeof(half)));
  CK(cudaMalloc(&C, n*sizeof(float)));
  CK(cudaMemset(A,0,n*sizeof(half)));
  CK(cudaMemset(B,0,n*sizeof(half)));
  float alpha=1.f, beta=0.f;
  const void *pa=&alpha,*pb=&beta;
  half ha=__float2half(1.f), hb=__float2half(0.f);
  cudaDataType_t cType = CUDA_R_32F;
  if(ct==CUBLAS_COMPUTE_16F){ pa=&ha; pb=&hb; cType=CUDA_R_16F; }
  // warmup
  for(int i=0;i<10;i++)
    CB(cublasGemmEx(h,CUBLAS_OP_N,CUBLAS_OP_N,N,N,N,pa,A,CUDA_R_16F,N,B,CUDA_R_16F,N,pb,C,cType,N,ct,CUBLAS_GEMM_DEFAULT_TENSOR_OP));
  CK(cudaDeviceSynchronize());
  cudaEvent_t evs,eve; CK(cudaEventCreate(&evs)); CK(cudaEventCreate(&eve));
  CK(cudaEventRecord(evs));
  for(int i=0;i<iters;i++)
    CB(cublasGemmEx(h,CUBLAS_OP_N,CUBLAS_OP_N,N,N,N,pa,A,CUDA_R_16F,N,B,CUDA_R_16F,N,pb,C,cType,N,ct,CUBLAS_GEMM_DEFAULT_TENSOR_OP));
  CK(cudaEventRecord(eve)); CK(cudaEventSynchronize(eve));
  float ms=0; CK(cudaEventElapsedTime(&ms,evs,eve));
  double tflops = 2.0*N*N*N*iters / (ms/1e3) / 1e12;
  cudaFree(A);cudaFree(B);cudaFree(C);
  cudaEventDestroy(evs);cudaEventDestroy(eve);
  return tflops;
}

int main(int argc,char**argv){
  int N = argc>1?atoi(argv[1]):8192;
  int iters = argc>2?atoi(argv[2]):50;
  int ndev=0; CK(cudaGetDeviceCount(&ndev));
  printf("GEMM %dx%dx%d, %d iters\n",N,N,N,iters);
  for(int d=0; d<ndev; d++){
    CK(cudaSetDevice(d));
    cudaDeviceProp p; CK(cudaGetDeviceProperties(&p,d));
    cublasHandle_t h; CB(cublasCreate(&h));
    double f32 = run(h,N,CUBLAS_COMPUTE_32F,iters);
    double f16 = run(h,N,CUBLAS_COMPUTE_16F,iters);
    printf("GPU%d %-20s  FP16->FP32acc: %7.1f TFLOPS   FP16->FP16acc(ctl): %7.1f TFLOPS\n",
           d,p.name,f32,f16);
    cublasDestroy(h);
  }
  return 0;
}
