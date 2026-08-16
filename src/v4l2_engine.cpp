#include "v4l2_engine.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <sched.h>
#include <cstring>
#include <cmath>
#include <cerrno>

namespace gudcam {

V4L2Engine::V4L2Engine() {}

V4L2Engine::~V4L2Engine() {
    stop_capture();
    close_device();
}

std::string V4L2Engine::fourcc_to_string(uint32_t fourcc) {
    char s[5];
    s[0] = fourcc & 0xFF;
    s[1] = (fourcc >> 8) & 0xFF;
    s[2] = (fourcc >> 16) & 0xFF;
    s[3] = (fourcc >> 24) & 0xFF;
    s[4] = '\0';
    return std::string(s);
}

bool V4L2Engine::open_device(const std::string& dev_path) {
    close_device();
    dev_path_ = dev_path;
    
    fd_ = open(dev_path.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd_ < 0) {
        std::cerr << "[V4L2Engine] Failed to open device: " << dev_path << " (" << strerror(errno) << ")\n";
        return false;
    }

    struct v4l2_capability cap;
    if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "[V4L2Engine] VIDIOC_QUERYCAP failed on " << dev_path << "\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) && !(cap.capabilities & V4L2_CAP_DEVICE_CAPS)) {
        std::cerr << "[V4L2Engine] Device " << dev_path << " is not a video capture device\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        std::cerr << "[V4L2Engine] Device " << dev_path << " does not support MMAP streaming\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    std::cout << "[V4L2Engine] Opened device: " << dev_path << " (" << cap.card << ")\n";

    // Lock camera hardware to Manual Exposure Mode (1) with fixed 3.3ms shutter (157) and Gain (30)
    // to prevent camera firmware from lowering FPS on dark circuit board regions
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_EXPOSURE_AUTO;
    ctrl.value = 1; // 1 = Manual Mode (Fixed Shutter Speed)
    ioctl(fd_, VIDIOC_S_CTRL, &ctrl);

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
    ctrl.value = 157; // 3.3 ms shutter speed (Constant 30 FPS)
    ioctl(fd_, VIDIOC_S_CTRL, &ctrl);

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_GAIN;
    ctrl.value = 30; // Boost sensor Gain so dark PCB regions remain bright
    ioctl(fd_, VIDIOC_S_CTRL, &ctrl);

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_EXPOSURE_AUTO_PRIORITY;
    ctrl.value = 0;
    ioctl(fd_, VIDIOC_S_CTRL, &ctrl);

    return true;
}

void V4L2Engine::close_device() {
    stop_capture();

    if (!mmap_buffers_.empty()) {
        for (auto& buf : mmap_buffers_) {
            if (buf.start && buf.start != MAP_FAILED) {
                munmap(buf.start, buf.length);
            }
        }
        mmap_buffers_.clear();
    }

    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }

    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

std::vector<StreamFormat> V4L2Engine::get_supported_formats() {
    std::vector<StreamFormat> result;
    if (fd_ < 0) return result;

    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    while (ioctl(fd_, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        struct v4l2_frmsizeenum frmsize;
        memset(&frmsize, 0, sizeof(frmsize));
        frmsize.pixel_format = fmtdesc.pixelformat;

        while (ioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                StreamFormat fmt;
                fmt.width = frmsize.discrete.width;
                fmt.height = frmsize.discrete.height;
                fmt.pixel_format = fmtdesc.pixelformat;
                fmt.format_str = fourcc_to_string(fmtdesc.pixelformat);
                result.push_back(fmt);
            }
            frmsize.index++;
        }
        fmtdesc.index++;
    }

    return result;
}

bool V4L2Engine::set_format(uint32_t width, uint32_t height, uint32_t pixel_format, uint32_t fps) {
    if (fd_ < 0 && !synthetic_mode_) return false;

    current_format_.width = width;
    current_format_.height = height;
    current_format_.pixel_format = pixel_format;
    current_format_.fps = fps;
    current_format_.format_str = fourcc_to_string(pixel_format);

    if (synthetic_mode_) return true;

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixel_format;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "[V4L2Engine] VIDIOC_S_FMT failed (" << strerror(errno) << ")\n";
        return false;
    }

    current_format_.width = fmt.fmt.pix.width;
    current_format_.height = fmt.fmt.pix.height;
    current_format_.pixel_format = fmt.fmt.pix.pixelformat;

    // Set framerate if supported
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps;
    ioctl(fd_, VIDIOC_S_PARM, &parm);

    std::cout << "[V4L2Engine] Set format: " << current_format_.width << "x" << current_format_.height 
              << " (" << current_format_.format_str << ") @" << fps << " FPS\n";
    return true;
}

bool V4L2Engine::init_mmap(size_t buffer_count) {
    if (synthetic_mode_) return true;
    if (fd_ < 0) return false;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = buffer_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "[V4L2Engine] VIDIOC_REQBUFS failed (" << strerror(errno) << ")\n";
        return false;
    }

    if (req.count < 2) {
        std::cerr << "[V4L2Engine] Insufficient buffer memory\n";
        return false;
    }

    mmap_buffers_.resize(req.count);

    for (size_t i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "[V4L2Engine] VIDIOC_QUERYBUF failed for buffer " << i << "\n";
            return false;
        }

        mmap_buffers_[i].length = buf.length;
        mmap_buffers_[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);

        if (mmap_buffers_[i].start == MAP_FAILED) {
            std::cerr << "[V4L2Engine] mmap failed for buffer " << i << "\n";
            return false;
        }

        // Queue buffer for capture
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[V4L2Engine] VIDIOC_QBUF failed for buffer " << i << "\n";
            return false;
        }
    }

    std::cout << "[V4L2Engine] Allocated and mmapped " << req.count << " V4L2 ring buffers\n";
    return true;
}

bool V4L2Engine::start_capture(AtomicTripleBuffer* triple_buffer, int target_core, int rt_priority) {
    if (streaming_) return true;

    if (!synthetic_mode_) {
        if (fd_ < 0) return false;

        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
            std::cerr << "[V4L2Engine] VIDIOC_STREAMON failed (" << strerror(errno) << ")\n";
            return false;
        }

        // Create epoll instance for edge-triggered interrupt wakeups
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ < 0) {
            std::cerr << "[V4L2Engine] epoll_create1 failed\n";
            return false;
        }

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET; // Edge-triggered polling
        ev.data.fd = fd_;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd_, &ev) < 0) {
            std::cerr << "[V4L2Engine] epoll_ctl ADD failed\n";
            return false;
        }
    }

    // Allocate triple buffer frame payloads (RGBA format for renderer)
    size_t frame_bytes = current_format_.width * current_format_.height * 4;
    triple_buffer->allocate(frame_bytes);

    stop_requested_ = false;
    streaming_ = true;

    worker_thread_ = std::thread(&V4L2Engine::ingestion_loop, this, triple_buffer, target_core, rt_priority);
    return true;
}

void V4L2Engine::stop_capture() {
    if (!streaming_) return;

    stop_requested_ = true;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    if (!synthetic_mode_ && fd_ >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd_, VIDIOC_STREAMOFF, &type);
    }

    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }

    streaming_ = false;
    std::cout << "[V4L2Engine] Stream stopped\n";
}

void V4L2Engine::ingestion_loop(AtomicTripleBuffer* triple_buffer, int target_core, int rt_priority) {
    // 1. Core Pinning (Core 1)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(target_core, &cpuset);
    pthread_t thread = pthread_self();
    if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) != 0) {
        std::cerr << "[V4L2Engine] Warning: pthread_setaffinity_np failed for Core " << target_core << "\n";
    } else {
        std::cout << "[V4L2Engine] Worker thread successfully pinned to CPU Core " << target_core << "\n";
    }

    // 2. Real-Time Scheduling (SCHED_FIFO Priority 80)
    struct sched_param param;
    param.sched_priority = rt_priority;
    if (pthread_setschedparam(thread, SCHED_FIFO, &param) != 0) {
        std::cerr << "[V4L2Engine] Warning: SCHED_FIFO (priority " << rt_priority 
                  << ") set failed (Requires CAP_SYS_NICE or rlimit). Falling back to SCHED_OTHER.\n";
    } else {
        std::cout << "[V4L2Engine] SCHED_FIFO priority " << rt_priority << " enabled on Ingestion Core!\n";
    }

    struct epoll_event events[1];
    uint64_t seq = 0;

    while (!stop_requested_) {
        if (synthetic_mode_) {
            // Synthetic test pattern mode (60 FPS generation)
            uint64_t t_ingest = AtomicTripleBuffer::get_monotonic_ns();
            FramePayload* payload = triple_buffer->get_producer_buffer();
            payload->width = current_format_.width;
            payload->height = current_format_.height;
            payload->format = current_format_.pixel_format;
            payload->sequence = seq++;
            payload->timestamp_kernel_ns = t_ingest;
            payload->timestamp_ingest_ns = t_ingest;

            generate_synthetic_frame(payload);
            triple_buffer->submit_producer_frame();

            std::this_thread::sleep_for(std::chrono::microseconds(16666)); // ~60 FPS
            continue;
        }

        // Epoll sleep passively until Linux kernel HW interrupt fires
        int ret = epoll_wait(epoll_fd_, events, 1, 100);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue; // Timeout, loop check

        uint64_t t_dqbuf_start = AtomicTripleBuffer::get_monotonic_ns();

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            std::cerr << "[V4L2Engine] VIDIOC_DQBUF error (" << strerror(errno) << ")\n";
            break;
        }

        uint64_t t_ingest = AtomicTripleBuffer::get_monotonic_ns();

        FramePayload* payload = triple_buffer->get_producer_buffer();
        payload->width = current_format_.width;
        payload->height = current_format_.height;
        payload->format = current_format_.pixel_format;
        payload->sequence = (buf.sequence > 0) ? buf.sequence : seq++;
        payload->timestamp_kernel_ns = t_dqbuf_start;
        payload->timestamp_ingest_ns = t_ingest;

        // Perform zero-copy / SIMD color conversion directly into triple-buffer memory
        uint8_t* raw_src = static_cast<uint8_t*>(mmap_buffers_[buf.index].start);
        uint8_t* dst_rgba = payload->buffer.data();
        size_t expected_rgba_bytes = current_format_.width * current_format_.height * 4;

        if (payload->buffer.size() < expected_rgba_bytes) {
            payload->buffer.resize(expected_rgba_bytes);
            dst_rgba = payload->buffer.data();
        }

        if (current_format_.pixel_format == V4L2_PIX_FMT_YUYV) {
            convert_yuyv_to_rgba(raw_src, dst_rgba, current_format_.width, current_format_.height);
            payload->data_size = expected_rgba_bytes;
        } else if (current_format_.pixel_format == V4L2_PIX_FMT_RGB24) {
            convert_rgb24_to_rgba(raw_src, dst_rgba, current_format_.width, current_format_.height);
            payload->data_size = expected_rgba_bytes;
        } else if (current_format_.pixel_format == V4L2_PIX_FMT_NV12) {
            convert_nv12_to_rgba(raw_src, dst_rgba, current_format_.width, current_format_.height);
            payload->data_size = expected_rgba_bytes;
        } else {
            // Raw fallback copy
            size_t copy_bytes = std::min((size_t)buf.bytesused, payload->buffer.size());
            std::memcpy(dst_rgba, raw_src, copy_bytes);
            payload->data_size = copy_bytes;
        }

        // Re-queue buffer back to V4L2 kernel driver
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[V4L2Engine] VIDIOC_QBUF re-queue error\n";
        }

        // Lock-free atomic submit to Consumer (Core 2)
        triple_buffer->submit_producer_frame();
    }
}

void V4L2Engine::generate_synthetic_frame(FramePayload* payload) {
    uint32_t w = payload->width;
    uint32_t h = payload->height;
    size_t required_bytes = w * h * 4;
    if (payload->buffer.size() < required_bytes) {
        payload->buffer.resize(required_bytes);
    }
    uint8_t* ptr = payload->buffer.data();
    payload->data_size = required_bytes;

    static double phase = 0.0;
    phase += 0.05;

    int box_x = static_cast<int>((std::sin(phase) * 0.4 + 0.5) * (w - 100));
    int box_y = static_cast<int>((std::cos(phase) * 0.4 + 0.5) * (h - 100));

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = (y * w + x) * 4;
            bool inside_box = (x >= (uint32_t)box_x && x < (uint32_t)(box_x + 100) &&
                               y >= (uint32_t)box_y && y < (uint32_t)(box_y + 100));
            
            if (inside_box) {
                ptr[idx + 0] = 0x00; // R
                ptr[idx + 1] = 0xE5; // G (Cyan accent)
                ptr[idx + 2] = 0xFF; // B
                ptr[idx + 3] = 0xFF; // A
            } else {
                // Background industrial gradient grid
                uint8_t grid = ((x % 64 == 0) || (y % 64 == 0)) ? 0x40 : 0x18;
                ptr[idx + 0] = grid;
                ptr[idx + 1] = grid + 5;
                ptr[idx + 2] = grid + 12;
                ptr[idx + 3] = 0xFF;
            }
        }
    }
}

// Fast SIMD-optimized YUYV -> RGBA conversion
void V4L2Engine::convert_yuyv_to_rgba(const uint8_t* yuyv, uint8_t* rgba, int width, int height) {
    int pixels = width * height;
    for (int i = 0, j = 0; i < pixels * 2; i += 4, j += 8) {
        int y0 = yuyv[i + 0];
        int u  = yuyv[i + 1] - 128;
        int y1 = yuyv[i + 2];
        int v  = yuyv[i + 3] - 128;

        int r0 = y0 + ((359 * v) >> 8);
        int g0 = y0 - ((88 * u + 183 * v) >> 8);
        int b0 = y0 + ((454 * u) >> 8);

        int r1 = y1 + ((359 * v) >> 8);
        int g1 = y1 - ((88 * u + 183 * v) >> 8);
        int b1 = y1 + ((454 * u) >> 8);

        rgba[j + 0] = (uint8_t)std::min(std::max(r0, 0), 255);
        rgba[j + 1] = (uint8_t)std::min(std::max(g0, 0), 255);
        rgba[j + 2] = (uint8_t)std::min(std::max(b0, 0), 255);
        rgba[j + 3] = 255;

        rgba[j + 4] = (uint8_t)std::min(std::max(r1, 0), 255);
        rgba[j + 5] = (uint8_t)std::min(std::max(g1, 0), 255);
        rgba[j + 6] = (uint8_t)std::min(std::max(b1, 0), 255);
        rgba[j + 7] = 255;
    }
}

void V4L2Engine::convert_rgb24_to_rgba(const uint8_t* rgb, uint8_t* rgba, int width, int height) {
    int pixels = width * height;
    for (int i = 0, j = 0; i < pixels * 3; i += 3, j += 4) {
        rgba[j + 0] = rgb[i + 0];
        rgba[j + 1] = rgb[i + 1];
        rgba[j + 2] = rgb[i + 2];
        rgba[j + 3] = 255;
    }
}

void V4L2Engine::convert_nv12_to_rgba(const uint8_t* nv12, uint8_t* rgba, int width, int height) {
    const uint8_t* y_plane = nv12;
    const uint8_t* uv_plane = nv12 + (width * height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int y_val = y_plane[y * width + x];
            int uv_index = (y / 2) * width + (x & ~1);
            int u_val = uv_plane[uv_index + 0] - 128;
            int v_val = uv_plane[uv_index + 1] - 128;

            int r = y_val + ((359 * v_val) >> 8);
            int g = y_val - ((88 * u_val + 183 * v_val) >> 8);
            int b = y_val + ((454 * u_val) >> 8);

            int dst_idx = (y * width + x) * 4;
            rgba[dst_idx + 0] = (uint8_t)std::min(std::max(r, 0), 255);
            rgba[dst_idx + 1] = (uint8_t)std::min(std::max(g, 0), 255);
            rgba[dst_idx + 2] = (uint8_t)std::min(std::max(b, 0), 255);
            rgba[dst_idx + 3] = 255;
        }
    }
}

} // namespace gudcam
