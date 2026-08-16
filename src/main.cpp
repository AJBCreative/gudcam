#include "v4l2_engine.hpp"
#include "v4l2_controls.hpp"
#include "atomic_triple_buffer.hpp"
#include "gpu_pipeline.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>

static std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "=====================================================\n";
    std::cout << "    GudCam Native Low-Latency Engine Baseline\n";
    std::cout << "=====================================================\n";

    std::string device_path = "/dev/video0";
    if (argc > 1) {
        device_path = argv[1];
    }

    gudcam::V4L2Engine engine;
    gudcam::V4L2Controls controls;
    gudcam::AtomicTripleBuffer triple_buffer;
    gudcam::GPUPipeline gpu;

    gpu.init();
    gpu.configure_thread_affinity(2, 50); // Pin consumer/render thread to Core 2, SCHED_FIFO 50

    bool open_ok = engine.open_device(device_path);
    if (!open_ok) {
        std::cout << "[Main] Device " << device_path << " unavailable. Enabling Synthetic Generator mode...\n";
        engine.set_synthetic_mode(true);
    }

    engine.set_format(640, 480, V4L2_PIX_FMT_YUYV, 30);
    engine.init_mmap(8);

    if (open_ok) {
        controls.query_all_controls(engine.get_fd());
    }

    // Start Ingestion loop on Core 1 with SCHED_FIFO priority 80
    engine.start_capture(&triple_buffer, 1, 80);

    std::vector<uint8_t> sr_rgba;
    uint32_t out_w = 0, out_h = 0;
    gudcam::PipelineMetrics metrics;

    std::cout << "\n[Main] Capture active. Press Ctrl+C to terminate.\n";
    auto start_time = std::chrono::steady_clock::now();

    while (g_running) {
        gudcam::FramePayload* frame = nullptr;
        if (triple_buffer.acquire_consumer_frame(&frame)) {
            gpu.process_frame(frame, sr_rgba, out_w, out_h, gudcam::ScalingMode::ESPCN_x2);
            gpu.update_metrics(frame, &triple_buffer, metrics);

            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() >= 1000) {
                std::cout << "[HUD Stats] Output: " << out_w << "x" << out_h << " (ESPCN x2) | "
                          << "Render FPS: " << metrics.render_fps << " | "
                          << "Produced: " << metrics.total_produced << " | "
                          << "Consumed: " << metrics.total_consumed << " | "
                          << "Drops: " << metrics.total_dropped << " | "
                          << "Total Latency: " << metrics.latency_total_ms << " ms\n";
                start_time = now;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "\n[Main] Shutting down...\n";
    engine.stop_capture();
    engine.close_device();
    std::cout << "[Main] Complete.\n";
    return 0;
}
