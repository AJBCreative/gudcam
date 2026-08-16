#include "atomic_triple_buffer.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <cassert>

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  Testing Zero-Lock Atomic Triple Buffer (High Load)\n";
    std::cout << "=========================================================\n";

    gudcam::AtomicTripleBuffer triple_buffer;
    triple_buffer.allocate(640 * 480 * 4); // Allocate 1.2MB frame buffers

    std::atomic<bool> start_flag{false};
    std::atomic<bool> stop_flag{false};

    const uint64_t TARGET_PRODUCED_FRAMES = 50000;

    // Producer Thread (Core 1 Ingestion simulation @ 1000 FPS)
    std::thread producer([&]() {
        while (!start_flag.load()) std::this_thread::yield();

        for (uint64_t i = 1; i <= TARGET_PRODUCED_FRAMES; ++i) {
            gudcam::FramePayload* frame = triple_buffer.get_producer_buffer();
            frame->sequence = i;
            frame->width = 640;
            frame->height = 480;
            
            // Fill validation stamp into buffer
            uint64_t* stamp = reinterpret_cast<uint64_t*>(frame->buffer.data());
            *stamp = i;

            triple_buffer.submit_producer_frame();

            // Simulate high rate (~1000 FPS)
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        stop_flag.store(true);
    });

    // Consumer Thread (Core 2 Render simulation @ 200 FPS)
    uint64_t valid_frames_read = 0;
    uint64_t last_seq_seen = 0;

    std::thread consumer([&]() {
        while (!start_flag.load()) std::this_thread::yield();

        while (!stop_flag.load() || triple_buffer.get_consumed_count() < 10) {
            gudcam::FramePayload* frame = nullptr;
            if (triple_buffer.acquire_consumer_frame(&frame)) {
                assert(frame != nullptr);
                uint64_t* stamp = reinterpret_cast<uint64_t*>(frame->buffer.data());
                
                // Verify lock-free data integrity
                assert(*stamp == frame->sequence);
                assert(frame->sequence >= last_seq_seen); // Sequence numbers must never decrease!

                last_seq_seen = frame->sequence;
                valid_frames_read++;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });

    // Start benchmark clock
    auto t_start = std::chrono::high_resolution_clock::now();
    start_flag.store(true);

    producer.join();
    consumer.join();

    auto t_end = std::chrono::high_resolution_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "[Test Results]\n";
    std::cout << "  - Total Produced: " << triple_buffer.get_produced_count() << " frames\n";
    std::cout << "  - Total Consumed: " << triple_buffer.get_consumed_count() << " frames\n";
    std::cout << "  - Total Dropped:  " << triple_buffer.get_dropped_count() << " frames\n";
    std::cout << "  - Validated Read: " << valid_frames_read << " frames without memory corruption!\n";
    std::cout << "  - Benchmark Time: " << duration_ms << " ms\n";

    assert(triple_buffer.get_produced_count() == TARGET_PRODUCED_FRAMES);
    uint64_t total_accounted = triple_buffer.get_consumed_count() + triple_buffer.get_dropped_count();
    assert(triple_buffer.get_produced_count() == total_accounted || 
           triple_buffer.get_produced_count() == total_accounted + 1);

    std::cout << "\n>>> Lock-Free Atomic Triple Buffer Test PASSED! <<<\n";
    return 0;
}
