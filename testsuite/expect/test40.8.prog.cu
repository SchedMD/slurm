// Slurm regression test40.8.prog.cu
#include <iostream>
#include <math.h>
#include <sys/time.h>
// Kernel function to add the elements of two arrays
__global__
void add(int n, float *x, float *y)
{
	int index = threadIdx.x;
	int stride = blockDim.x;
	for (int i = index; i < n; i += stride)
		y[i] = x[i] + y[i];
}

int main(void)
{
	int N = 1024 * 1024 * 16;
	int i;
	float *x, *y;
	float maxError = 0.0f;
	struct timeval tv1, tv2;
	int delta_t;

	// Get start time
	gettimeofday(&tv1, NULL);

	// Allocate Unified Memory – accessible from CPU or GPU
	cudaMallocManaged(&x, N * sizeof(float));
	cudaMallocManaged(&y, N * sizeof(float));

	// initialize x and y arrays on the host
	for (i = 0; i < N; i++) {
		x[i] = 1.0f;
		y[i] = 2.0f;
	}

	// Run kernel on 256 elements at a time on the GPU
	add<<<1, 256>>>(N, x, y);

	// Wait for GPU to finish before accessing on host
	cudaDeviceSynchronize();

	// Check for errors (all values should be 3.0f)
	for (i = 0; i < N; i++)
		maxError = fmax(maxError, fabs(y[i] - 3.0f));
	std::cout << "Max error: " << maxError << std::endl;

	// Free memory
	cudaFree(x);
	cudaFree(y);

	// Get start time
	gettimeofday(&tv2, NULL);
	delta_t  = (tv2.tv_sec  - tv1.tv_sec) * 1000000;
	delta_t += (tv2.tv_usec - tv1.tv_usec);
	std::cout << "Run Time (usec): " << delta_t << std::endl;

	return 0;
}
