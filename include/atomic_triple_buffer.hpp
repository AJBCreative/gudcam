#ifndef ATOMIC_TRIPLE_BUFFER_HPP
#define ATOMIC_TRIPLE_BUFFER_HPP

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>

namespace gudcam {

/**
 * High-precision timestamp & frame payload metadata
 */
struct FramePayload {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t format{0}; // V4L2 fourcc code
    uint32_t stride{0};
    size_t data_size{0};
    uint64_t sequence{0};
    
    // Microsecond / Nanosecond timing metrics
    uint64_t timestamp_kernel_ns{0}; // Kernel monotonic timestamp from v4l2_buffer
    uint64_t timestamp_ingest_ns{0}; // Time when Core 1 epoll received frame
    uint64_t timestamp_swap_ns{0};   // Time when producer committed to triple buffer
    uint64_t timestamp_consumer_ns{0};// Time when Core 2 render fetched frame
    
    std::vector<uint8_t> buffer;     // Pixel data storage
};

/**
 * Lock-Free Atomic Triple Buffer
 *
 * Implements a zero-lock, zero-mutex atomic index exchange between 
 * Ingestion Core 1 (Producer) and Render Core 2 (Consumer).
 * Uses hardware atomic XCHG instructions with acquire/release memory semantics.
 */
class AtomicTripleBuffer {
public:
    AtomicTripleBuffer() 
        : shared_idx_(1),
          producer_idx_(0),
          consumer_idx_(2),
          new_frame_flag_(false),
          produced_frames_(0),
          consumed_frames_(0),
          dropped_frames_(0)
    {
    }

    // Initialize underlying buffer sizes
    void allocate(size_t max_size) {
        for (int i = 0; i < 3; ++i) {
            frames_[i].buffer.resize(max_size);
        }
    }

    // Get pointer to the buffer current producer (Core 1) is writing into
    FramePayload* get_producer_buffer() {
        return &frames_[producer_idx_];
    }

    // Submit newly ingested frame from Producer (Core 1) to Consumer (Core 2)
    void submit_producer_frame() {
        FramePayload* frame = &frames_[producer_idx_];
        frame->timestamp_swap_ns = get_monotonic_ns();
        
        // Atomically exchange producer index with shared index
        int prev_shared = shared_idx_.exchange(producer_idx_, std::memory_order_acq_rel);
        producer_idx_ = prev_shared;
        
        // Check if previous frame was overwritten before consumer read it (frame drop counter)
        if (new_frame_flag_.exchange(true, std::memory_order_acq_rel)) {
            dropped_frames_.fetch_add(1, std::memory_order_relaxed);
        }
        
        produced_frames_.fetch_add(1, std::memory_order_relaxed);
    }

    // Consumer (Core 2) acquires the latest ready frame (Lock-Free)
    // Returns true if a new frame was available, false if consuming same buffer
    bool acquire_consumer_frame(FramePayload** out_frame) {
        bool has_new = new_frame_flag_.exchange(false, std::memory_order_acq_rel);
        
        if (has_new) {
            // Atomically exchange consumer index with shared index
            int prev_shared = shared_idx_.exchange(consumer_idx_, std::memory_order_acq_rel);
            consumer_idx_ = prev_shared;
            consumed_frames_.fetch_add(1, std::memory_order_relaxed);
        }
        
        FramePayload* frame = &frames_[consumer_idx_];
        if (has_new) {
            frame->timestamp_consumer_ns = get_monotonic_ns();
        }
        
        if (out_frame) {
            *out_frame = frame;
        }
        return has_new;
    }

    // Current metrics
    uint64_t get_produced_count() const { return produced_frames_.load(std::memory_order_relaxed); }
    uint64_t get_consumed_count() const { return consumed_frames_.load(std::memory_order_relaxed); }
    uint64_t get_dropped_count() const { return dropped_frames_.load(std::memory_order_relaxed); }

    static uint64_t get_monotonic_ns() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    }

private:
    FramePayload frames_[3];
    std::atomic<int> shared_idx_;
    int producer_idx_;
    int consumer_idx_;
    std::atomic<bool> new_frame_flag_;

    std::atomic<uint64_t> produced_frames_;
    std::atomic<uint64_t> consumed_frames_;
    std::atomic<uint64_t> dropped_frames_;
};

} // namespace gudcam

#endif // ATOMIC_TRIPLE_BUFFER_HPP
