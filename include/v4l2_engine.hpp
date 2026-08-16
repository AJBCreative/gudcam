#ifndef V4L2_ENGINE_HPP
#define V4L2_ENGINE_HPP

#include "atomic_triple_buffer.hpp"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <linux/videodev2.h>

namespace gudcam {

struct V4L2BufferMap {
    void* start{nullptr};
    size_t length{0};
};

struct StreamFormat {
    uint32_t width{640};
    uint32_t height{480};
    uint32_t pixel_format{V4L2_PIX_FMT_YUYV};
    uint32_t fps{30};
    std::string format_str{"YUYV"};
};

class V4L2Engine {
public:
    V4L2Engine();
    ~V4L2Engine();

    // Open video device (e.g. /dev/video0)
    bool open_device(const std::string& dev_path);
    void close_device();

    // Configure stream resolution and format
    bool set_format(uint32_t width, uint32_t height, uint32_t pixel_format, uint32_t fps = 30);
    
    // Request and mmap ring buffer (default 8 buffers)
    bool init_mmap(size_t buffer_count = 8);

    // Start background ingestion thread pinned to core_id (default Core 1) with real-time priority
    bool start_capture(AtomicTripleBuffer* triple_buffer, int target_core = 1, int rt_priority = 80);
    
    // Stop worker thread and stream
    void stop_capture();

    // Utility formats conversion
    static std::string fourcc_to_string(uint32_t fourcc);
    static void convert_yuyv_to_rgba(const uint8_t* yuyv, uint8_t* rgba, int width, int height);
    static void convert_rgb24_to_rgba(const uint8_t* rgb, uint8_t* rgba, int width, int height);
    static void convert_nv12_to_rgba(const uint8_t* nv12, uint8_t* rgba, int width, int height);

    // Stream info getters
    bool is_streaming() const { return streaming_.load(); }
    int get_fd() const { return fd_; }
    const StreamFormat& get_format() const { return current_format_; }
    std::vector<StreamFormat> get_supported_formats();

    // Set synthetic mode (for testing without physical camera)
    void set_synthetic_mode(bool enabled) { synthetic_mode_ = enabled; }
    bool is_synthetic_mode() const { return synthetic_mode_; }

private:
    void ingestion_loop(AtomicTripleBuffer* triple_buffer, int target_core, int rt_priority);
    void generate_synthetic_frame(FramePayload* payload);

    std::string dev_path_;
    int fd_{-1};
    int epoll_fd_{-1};
    
    std::vector<V4L2BufferMap> mmap_buffers_;
    StreamFormat current_format_;
    
    std::atomic<bool> streaming_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread worker_thread_;

    bool synthetic_mode_{false};
    uint64_t synthetic_seq_{0};
};

} // namespace gudcam

#endif // V4L2_ENGINE_HPP
