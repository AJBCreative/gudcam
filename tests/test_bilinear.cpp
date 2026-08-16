#include <iostream>
#include <chrono>
#include <vector>
#include "gpu_pipeline.hpp"

using namespace gudcam;

int main() {
    GPUPipeline pipeline;
    pipeline.init();
    FramePayload frame;
    frame.width = 640;
    frame.height = 480;
    frame.buffer.resize(640 * 480 * 4, 128);
    std::vector<uint8_t> dst(1280 * 960 * 4, 0);
    
    uint32_t out_w, out_h;
    
    auto t0 = std::chrono::high_resolution_clock::now();
    pipeline.process_frame(&frame, dst, out_w, out_h, ScalingMode::Bilinear);
    auto t1 = std::chrono::high_resolution_clock::now();
    
    std::cout << "Bilinear x2 Time: " << std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0 << " ms" << std::endl;
    return 0;
}
