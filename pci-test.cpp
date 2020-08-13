#include <cuda_runtime.h>
#include <curand.h>
#include <iostream>

#include "timer.hpp"
#include "utils.hpp"

int main() {
    auto bandwidth = [](size_t size, uint64_t time) {
        double speed = static_cast<double>(size) / time * 1e9;
        return speed / static_cast<double>(1u << 30u);
    };

    for (size_t size: {Unit::B(4), Unit::KiB(1), Unit::MiB(1), Unit::GiB(1), Unit::GiB(2), Unit::GiB(4)}) {
        printf("Running test with %s ... \n", prettyBytes(size).c_str());

        // Init
        void *host_ptr, *device_ptr;
        cudaMallocHost(&host_ptr, size, cudaHostAllocDefault);
        cudaMalloc(&device_ptr, size);
        curandGenerator_t generator;
        curandCreateGeneratorHost(&generator, CURAND_RNG_PSEUDO_DEFAULT);
        curandGenerateNormal(generator, static_cast<float*>(host_ptr), size / sizeof(float), 0, 0.1);

        // Transfer
        Timer timer;
        cudaMemcpy(device_ptr, host_ptr, size, cudaMemcpyHostToDevice);
        auto host2device_duration = timer.tik();
        cudaMemcpy(host_ptr, device_ptr, size, cudaMemcpyDeviceToHost);
        auto device2host_duration = timer.tik();

        // Finish
        cudaFreeHost(host_ptr);
        cudaFree(device_ptr);
        printf(" > Host to device: %s (%.3lf GiB/s)\n", prettyNanoseconds(host2device_duration).c_str(), bandwidth(size, host2device_duration));
        printf(" > Device to host: %s (%.3lf GiB/s)\n", prettyNanoseconds(device2host_duration).c_str(), bandwidth(size, device2host_duration));
    }
    return 0;
}