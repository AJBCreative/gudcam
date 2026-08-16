#include "v4l2_engine.hpp"
#include "v4l2_controls.hpp"
#include "atomic_triple_buffer.hpp"
#include "gpu_pipeline.hpp"

#include <iostream>
#include <vector>
#include <cstring>

struct GudCamContext {
    gudcam::V4L2Engine engine;
    gudcam::V4L2Controls controls;
    gudcam::AtomicTripleBuffer triple_buffer;
    gudcam::GPUPipeline gpu_pipeline;
    std::vector<uint8_t> processed_frame_buf;
    gudcam::PipelineMetrics current_metrics;
};

extern "C" {

void* gudcam_create_context() {
    GudCamContext* ctx = new GudCamContext();
    ctx->gpu_pipeline.init();
    return ctx;
}

void gudcam_destroy_context(void* ctx_ptr) {
    if (ctx_ptr) {
        GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);
        delete ctx;
    }
}

bool gudcam_open_device(void* ctx_ptr, const char* dev_path) {
    if (!ctx_ptr || !dev_path) return false;
    GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);
    return ctx->engine.open_device(dev_path);
}

void gudcam_close_device(void* ctx_ptr) {
    if (!ctx_ptr) return;
    GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);
    ctx->engine.close_device();
}

void gudcam_set_synthetic(void* ctx_ptr, bool enabled) {
    if (!ctx_ptr) return;
    GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);
    ctx->engine.set_synthetic_mode(enabled);
}

bool gudcam_set_format(void* ctx_ptr, uint32_t width, uint32_t height, uint32_t fourcc, uint32_t fps) {
    if (!ctx_ptr) return false;
    GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);
    return ctx->engine.set_format(width, height, fourcc, fps);
}

bool gudcam_init_mmap(void* ctx_ptr, size_t buffer_count) {
    if (!ctx_ptr) return false;
    GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);
    return ctx->engine.init_mmap(buffer_count);
}

bool gudcam_start_capture(void* ctx_ptr, int core1, int prio1) {
    if (!ctx_ptr) return false;
    GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);
    return ctx->engine.start_capture(&ctx->triple_buffer, core1, prio1);
}

void gudcam_stop_capture(void* ctx_ptr) {
    if (!ctx_ptr) return;
    GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);
    ctx->engine.stop_capture();
}

bool gudcam_configure_render_thread(int core2, int prio2) {
    return gudcam::GPUPipeline::configure_thread_affinity(core2, prio2);
}

bool gudcam_acquire_processed_frame(void* ctx_ptr, 
                                     uint8_t* out_buffer, 
                                     size_t buffer_capacity, 
                                     uint32_t* out_w, 
                                     uint32_t* out_h, 
                                     uint64_t* out_seq, 
                                     int sr_mode,
                                     double* out_latency_ms,
                                     double* out_render_fps,
                                     uint64_t* out_drops,
                                     double* out_focus_score,
                                     bool hardware_acceleration) {
    if (!ctx_ptr) return false;
    GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);

    gudcam::FramePayload* frame = nullptr;
    bool has_new = ctx->triple_buffer.acquire_consumer_frame(&frame);
    
    if (frame && frame->data_size > 0 && has_new) {
        uint32_t sr_w = frame->width;
        uint32_t sr_h = frame->height;
        
        if (!hardware_acceleration) {
            ctx->gpu_pipeline.process_frame(frame, ctx->processed_frame_buf, sr_w, sr_h, 
                                           static_cast<gudcam::ScalingMode>(sr_mode));

            size_t required = sr_w * sr_h * 4;
            if (out_buffer && buffer_capacity >= required) {
                std::memcpy(out_buffer, ctx->processed_frame_buf.data(), required);
            }
        } else {
            // Hardware compute shader mode: bypass CPU scaling completely.
            // Just copy the raw base-resolution RGBA payload directly.
            size_t required = frame->data_size;
            if (out_buffer && buffer_capacity >= required) {
                std::memcpy(out_buffer, frame->buffer.data(), required);
            }
        }

        if (out_w) *out_w = sr_w;
        if (out_h) *out_h = sr_h;
        if (out_seq) *out_seq = frame->sequence;

        ctx->gpu_pipeline.update_metrics(frame, &ctx->triple_buffer, ctx->current_metrics);

        if (out_latency_ms) *out_latency_ms = ctx->current_metrics.latency_total_ms;
        if (out_render_fps) *out_render_fps = ctx->current_metrics.render_fps;
        if (out_drops) *out_drops = ctx->current_metrics.total_dropped;
        if (out_focus_score) *out_focus_score = ctx->current_metrics.focus_score;

        return true; // Always return true to trigger GUI texture update (120 FPS!)
    }

    return false;
}

int gudcam_query_controls_json(void* ctx_ptr, char* json_out, size_t max_len) {
    if (!ctx_ptr || !json_out) return 0;
    GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);

    int fd = ctx->engine.get_fd();
    auto controls = ctx->controls.query_all_controls(fd);

    std::string json = "[";
    for (size_t i = 0; i < controls.size(); ++i) {
        const auto& c = controls[i];
        json += "{";
        json += "\"id\":" + std::to_string(c.id) + ",";
        json += "\"name\":\"" + c.name + "\",";
        json += "\"type\":\"" + gudcam::V4L2Controls::control_type_to_string(c.type) + "\",";
        json += "\"min\":" + std::to_string(c.min) + ",";
        json += "\"max\":" + std::to_string(c.max) + ",";
        json += "\"step\":" + std::to_string(c.step) + ",";
        json += "\"default\":" + std::to_string(c.default_val) + ",";
        json += "\"value\":" + std::to_string(c.current_val) + ",";
        json += "\"menu\":[";
        for (size_t m = 0; m < c.menu_items.size(); ++m) {
            json += "{\"index\":" + std::to_string(c.menu_items[m].index) + ",\"name\":\"" + c.menu_items[m].name + "\"}";
            if (m + 1 < c.menu_items.size()) json += ",";
        }
        json += "]}";
        if (i + 1 < controls.size()) json += ",";
    }
    json += "]";

    size_t copy_len = std::min(json.size(), max_len - 1);
    std::strncpy(json_out, json.c_str(), copy_len);
    json_out[copy_len] = '\0';

    return static_cast<int>(copy_len);
}

bool gudcam_set_control(void* ctx_ptr, uint32_t ctrl_id, int32_t value) {
    if (!ctx_ptr) return false;
    GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);
    int fd = ctx->engine.get_fd();
    if (fd < 0 && !ctx->engine.is_synthetic_mode()) return false;
    return ctx->controls.set_control(fd, ctrl_id, value);
}

void gudcam_set_microscope_params(void* ctx_ptr, float zoom_factor, float pan_x, float pan_y, float focus_offset, bool focus_peaking, bool hdr_boost, bool optical_boost, bool ar_reconstruction, bool stabilization_enabled, bool temporal_fusion, bool frame_generation) {
    if (!ctx_ptr) return;
    GudCamContext* ctx = static_cast<GudCamContext*>(ctx_ptr);
    gudcam::DigitalMicroscopeParams params;
    params.zoom_factor = zoom_factor;
    params.pan_x = pan_x;
    params.pan_y = pan_y;
    params.focus_offset = focus_offset;
    params.focus_peaking = focus_peaking;
    params.hdr_boost = hdr_boost;
    params.optical_boost = optical_boost;
    params.ar_reconstruction = ar_reconstruction;
    params.stabilization_enabled = stabilization_enabled;
    params.temporal_fusion = temporal_fusion;
    params.frame_generation = frame_generation;
    ctx->gpu_pipeline.set_microscope_params(params);
}

} // extern "C"
