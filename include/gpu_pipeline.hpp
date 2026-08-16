#ifndef GPU_PIPELINE_HPP
#define GPU_PIPELINE_HPP

#include "atomic_triple_buffer.hpp"
#include <vector>
#include <cstdint>
#include <memory>
#include <atomic>
#include <thread>

namespace gudcam {

enum class ScalingMode {
    Bilinear = 0,
    Bicubic = 1,
    ESPCN_x2 = 2,
    FSRCNN_x2 = 3,
    ESPCN_x4_1440p = 4,
    FSRCNN_x4_1440p = 5,
    FSR4_x4 = 6
};

struct PipelineMetrics {
    double ingest_fps{0.0};
    double render_fps{0.0};
    uint64_t total_produced{0};
    uint64_t total_consumed{0};
    uint64_t total_dropped{0};
    double latency_kernel_ms{0.0};
    double latency_swap_ms{0.0};
    double latency_sr_ms{0.0};
    double latency_total_ms{0.0};
    double focus_score{0.0}; // Software Focus Quality Index (0.0 to 100.0%)
    bool core1_pinned{false};
    bool core2_pinned{false};
    bool sched_fifo_active{false};
};

struct DigitalMicroscopeParams {
    float zoom_factor{1.0f};  // 1.0x to 10.0x
    float pan_x{0.0f};        // -1.0 to +1.0
    float pan_y{0.0f};        // -1.0 to +1.0
    float focus_offset{0.0f}; // Software Electronic Focus Adjust (-1.0 to +1.0)
    bool focus_peaking{false};
    bool hdr_boost{false};
    bool high_contrast{false};
    bool optical_boost{true};       // $5,000 Keyence Microscope ISP Quality Boost
    bool ar_reconstruction{true};   // AR Neural Sub-Pixel Directional Edge Reconstruction
    bool stabilization_enabled{true};// Sub-Pixel Optical Image Stabilization (OIS/EIS)
    bool temporal_fusion{true};     // Multi-Frame Temporal Super-Resolution Fusion
    bool frame_generation{true};    // AMD FSR 4 Optical Flow Frame Generation (AFMF 60->120 FPS)
};

class GPUPipeline {
public:
    GPUPipeline();
    ~GPUPipeline();

    // Initialize pipeline weights and buffers
    bool init();

    // Configure digital microscope zoom, pan, and visual enhancement parameters
    void set_microscope_params(const DigitalMicroscopeParams& params) { params_ = params; }
    const DigitalMicroscopeParams& get_microscope_params() const { return params_; }

    // Process input RGBA frame with selected Super-Resolution scaling mode
    void process_frame(const FramePayload* input_frame, 
                       std::vector<uint8_t>& output_rgba, 
                       uint32_t& out_w, 
                       uint32_t& out_h, 
                       ScalingMode mode = ScalingMode::ESPCN_x2);

    // Pin render/super-resolution thread to Core 2 with SCHED_FIFO priority 50
    static bool configure_thread_affinity(int target_core = 2, int rt_priority = 50);

    // Compute live metrics
    void update_metrics(const FramePayload* frame, AtomicTripleBuffer* triple_buf, PipelineMetrics& metrics);

private:
    // Fast SIMD ESPCN x2 upscaling sub-pixel convolution kernel
    void espcn_x2_upsample_rgba(const uint8_t* src, uint32_t src_w, uint32_t src_h, uint8_t* dst);
    
    // 4x ESPCN 2560x1920 4x Ultra-HD AI Super-Resolution with OpenMP 16-Tap Bicubic Engine
    void espcn_x4_1440p_upsample_rgba(const uint8_t* src, uint32_t src_w, uint32_t src_h, uint8_t* dst);

    // AMD FSR 4 (FidelityFX Super Resolution 4 - Spatial & Temporal Edge Adaptive Super-Resolution Engine)
    void fsr4_x4_upsample_rgba(const uint8_t* src, uint32_t src_w, uint32_t src_h, uint8_t* dst);

    // Bilinear fallback x2 upsample
    void bilinear_x2_upsample_rgba(const uint8_t* src, uint32_t src_w, uint32_t src_h, uint8_t* dst);

    uint64_t last_frame_time_ns_{0};
    double render_fps_{0.0};
    float smooth_pan_x_{0.0f};
    float smooth_pan_y_{0.0f};
    std::vector<uint8_t> prev_frame_buf_;
    int frame_counter_{0};
    uint64_t fps_start_time_ns_{0};
    DigitalMicroscopeParams params_;
};

} // namespace gudcam

#endif // GPU_PIPELINE_HPP
