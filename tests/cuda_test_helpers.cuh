#ifndef CXPR_CUDA_TEST_HELPERS_CUH
#define CXPR_CUDA_TEST_HELPERS_CUH

#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>

namespace cxpr_cuda_test {

inline void check(cudaError_t status, const char* operation) {
    if (status == cudaSuccess) return;
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(status));
    std::exit(2);
}

inline unsigned blocks(size_t count, unsigned threads = 256u) {
    return static_cast<unsigned>((count + threads - 1u) / threads);
}

inline void synchronize(const char* operation) {
    check(cudaGetLastError(), operation);
    check(cudaDeviceSynchronize(), operation);
}

template <typename T>
class device_buffer {
public:
    explicit device_buffer(size_t count, const char* operation)
        : count_(count) {
        check(cudaMalloc(&data_, count * sizeof(T)), operation);
    }

    ~device_buffer() { cudaFree(data_); }

    device_buffer(const device_buffer&) = delete;
    device_buffer& operator=(const device_buffer&) = delete;

    T* data() { return data_; }
    const T* data() const { return data_; }
    size_t size() const { return count_; }

    void clear(const char* operation) {
        check(cudaMemset(data_, 0, count_ * sizeof(T)), operation);
    }

    void upload(const T* source, size_t count, const char* operation) {
        check(cudaMemcpy(data_, source, count * sizeof(T), cudaMemcpyHostToDevice),
              operation);
    }

    void download(T* destination, size_t count, const char* operation) const {
        check(cudaMemcpy(destination, data_, count * sizeof(T), cudaMemcpyDeviceToHost),
              operation);
    }

private:
    T* data_ = nullptr;
    size_t count_ = 0u;
};

} // namespace cxpr_cuda_test

#endif
