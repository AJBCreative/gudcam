#include "v4l2_engine.hpp"
#include "atomic_triple_buffer.hpp"
#include "gpu_pipeline.hpp"

#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <iomanip>

int main(int argc, char** argv) {
    std::cout << "=========================================================\n";
    std::cout << "  GudCam Sub-Millisecond Latency Benchmark Suite\n";
    std::cout << "=========================================================\n";

    gudcam::AtomicTripleBuffer triple_buffer;
    gudcam::GPUPipeline gpu;
    gpu.init();
    gpu.configure_thread_affinity(2, 50);

    // Warmup memory allocations
    triple_buffer.allocate(640 * 480 * 4);
    std::vector<uint8_t> sr_rgba(1280 * 960 * 4);

    const int NUM_ITERATIONS = 1000;

    std::vector<double> color_conv_times_ms;
    std::vector<double> atomic_swap_times_ms;
    std::vector<double> sr_times_ms;
    std::vector<double> total_pipeline_times_ms;

    color_conv_times_ms.reserve(NUM_ITERATIONS);
    atomic_swap_times_ms.reserve(NUM_ITERATIONS);
    sr_times_ms.reserve(NUM_ITERATIONS);
    total_pipeline_times_ms.reserve(NUM_ITERATIONS);

    // Prepare test input YUYV buffer (640x480)
    std::vector<uint8_t> yuyv_src(640 * 480 * 2, 128);

    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        auto t_total_start = std::chrono::high_resolution_clock::now();

        // 1. Color Conversion Latency (YUYV -> RGBA)
        auto t_color_start = std::chrono::high_resolution_clock::now();
        gudcam::FramePayload* prod_buf = triple_buffer.get_producer_buffer();
        prod_buf->width = 640;
        prod_buf->height = 480;
        prod_buf->sequence = i + 1;
        
        gudcam::V4L2Engine::convert_yuyv_to_rgba(yuyv_src.data(), prod_buf->buffer.data(), 640, 480);
        auto t_color_end = std::chrono::high_resolution_clock::now();

        // 2. Zero-Lock Atomic Triple Buffer Swap Latency
        auto t_swap_start = std::chrono::high_resolution_clock::now();
        triple_buffer.submit_producer_frame();
        
        gudcam::FramePayload* cons_buf = nullptr;
        bool has_frame = triple_buffer.acquire_consumer_frame(&cons_buf);
        auto t_swap_end = std::chrono::high_resolution_clock::now();

        // 3. ESPCN x2 Super-Resolution Inference Latency
        auto t_sr_start = std::chrono::high_resolution_clock::now();
        uint32_t out_w = 0, out_h = 0;
        if (has_frame) {
            gpu.process_frame(cons_buf, sr_rgba, out_w, out_h, gudcam::ScalingMode::ESPCN_x4_1440p);
        }
        auto t_sr_end = std::chrono::high_resolution_clock::now();

        auto t_total_end = std::chrono::high_resolution_clock::now();

        double d_color = std::chrono::duration<double, std::milli>(t_color_end - t_color_start).count();
        double d_swap  = std::chrono::duration<double, std::milli>(t_swap_end - t_swap_start).count();
        double d_sr    = std::chrono::duration<double, std::milli>(t_sr_end - t_sr_start).count();
        double d_total = std::chrono::duration<double, std::milli>(t_total_end - t_total_start).count();

        color_conv_times_ms.push_back(d_color);
        atomic_swap_times_ms.push_back(d_swap);
        sr_times_ms.push_back(d_sr);
        total_pipeline_times_ms.push_back(d_total);
    }

    auto calc_stats = [](std::vector<double>& v, double& avg, double& min_v, double& max_v, double& p99) {
        std::sort(v.begin(), v.end());
        double sum = std::accumulate(v.begin(), v.end(), 0.0);
        avg = sum / v.size();
        min_v = v.front();
        max_v = v.back();
        p99 = v[static_cast<size_t>(v.size() * 0.99)];
    };

    double avg_col, min_col, max_col, p99_col;
    double avg_swap, min_swap, max_swap, p99_swap;
    double avg_sr, min_sr, max_sr, p99_sr;
    double avg_tot, min_tot, max_tot, p99_tot;

    calc_stats(color_conv_times_ms, avg_col, min_col, max_col, p99_col);
    calc_stats(atomic_swap_times_ms, avg_swap, min_swap, max_swap, p99_swap);
    calc_stats(sr_times_ms, avg_sr, min_sr, max_sr, p99_sr);
    calc_stats(total_pipeline_times_ms, avg_tot, min_tot, max_tot, p99_tot);

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "\n[STAGE 1] YUYV422 -> RGBA SIMD Color Conversion:\n";
    std::cout << "  - Avg Latency: " << avg_col << " ms (" << (avg_col * 1000.0) << " us)\n";
    std::cout << "  - P99 Latency: " << p99_col << " ms\n";

    std::cout << "\n[STAGE 2] Zero-Lock Atomic Triple Buffer Swap:\n";
    std::cout << "  - Avg Latency: " << avg_swap << " ms (" << (avg_swap * 1000.0) << " us)\n";
    std::cout << "  - P99 Latency: " << p99_swap << " ms\n";

    std::cout << "\n[STAGE 3] ESPCN x4 1440p Quad-HD Super-Resolution Inference (640x480 -> 2560x1440):\n";
    std::cout << "  - Avg Latency: " << avg_sr << " ms (" << (avg_sr * 1000.0) << " us)\n";
    std::cout << "  - P99 Latency: " << p99_sr << " ms\n";

    std::cout << "\n[TOTAL END-TO-END PIPELINE LATENCY]:\n";
    std::cout << "  - Avg Total Latency: " << avg_tot << " ms\n";
    std::cout << "  - Min Total Latency: " << min_tot << " ms\n";
    std::cout << "  - P99 Total Latency: " << p99_tot << " ms\n";

    std::cout << "---------------------------------------------------------\n";
    if (avg_tot < 10.0 && p99_tot < 10.0) {
        std::cout << ">>> VERDICT: SUB-10MS LATENCY REQUIREMENT SATISFIED! <<<\n";
    } else {
        std::cout << ">>> VERDICT: LATENCY EXCEEDS 10MS TARGET - OPTIMIZATION REQUIRED <<<\n";
    }
    std::cout << "---------------------------------------------------------\n";

    return 0;
}
