#include "v4l2_engine.hpp"
#include "v4l2_controls.hpp"
#include "atomic_triple_buffer.hpp"
#include <iostream>
#include <cassert>

int main(int argc, char** argv) {
    std::string dev_path = "/dev/video0";
    if (argc > 1) dev_path = argv[1];

    std::cout << "=========================================================\n";
    std::cout << "  Testing V4L2 Ingestion Engine & Dynamic Controls\n";
    std::cout << "=========================================================\n";

    gudcam::V4L2Engine engine;
    gudcam::V4L2Controls controls;

    bool open_ok = engine.open_device(dev_path);
    if (!open_ok) {
        std::cout << "[Test V4L2] Physical camera " << dev_path << " unavailable. Testing synthetic pattern mode...\n";
        engine.set_synthetic_mode(true);
    }

    bool fmt_ok = engine.set_format(640, 480, V4L2_PIX_FMT_YUYV, 30);
    assert(fmt_ok);

    bool mmap_ok = engine.init_mmap(8);
    assert(mmap_ok);

    if (open_ok) {
        auto ctrl_list = controls.query_all_controls(engine.get_fd());
        std::cout << "[Test V4L2] Enumerated " << ctrl_list.size() << " dynamic hardware controls:\n";
        for (const auto& c : ctrl_list) {
            std::cout << "  - ID: 0x" << std::hex << c.id << std::dec 
                      << " | Name: " << c.name 
                      << " | Type: " << gudcam::V4L2Controls::control_type_to_string(c.type)
                      << " | Range: [" << c.min << ".." << c.max << "]"
                      << " | Current: " << c.current_val << "\n";
        }
    }

    gudcam::AtomicTripleBuffer triple_buffer;
    bool start_ok = engine.start_capture(&triple_buffer, 1, 80);
    assert(start_ok);

    std::cout << "[Test V4L2] Streaming active for 2 seconds...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    assert(triple_buffer.get_produced_count() > 0);
    std::cout << "  - Captured " << triple_buffer.get_produced_count() << " frames successfully!\n";

    engine.stop_capture();
    engine.close_device();

    std::cout << "\n>>> Low-Level V4L2 Engine Test PASSED! <<<\n";
    return 0;
}
