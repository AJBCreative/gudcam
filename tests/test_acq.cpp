#include <iostream>
#include <chrono>
#include "atomic_triple_buffer.hpp"

using namespace gudcam;

int main() {
    AtomicTripleBuffer tb;
    tb.allocate(1024);
    FramePayload* frame = nullptr;
    auto t0 = std::chrono::high_resolution_clock::now();
    bool has_new = tb.acquire_consumer_frame(&frame);
    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "Time: " << std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() << " ns, has_new: " << has_new << std::endl;
    return 0;
}
