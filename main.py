#!/usr/bin/env python3
import sys
import os
import time

def main():
    dev_path = "/dev/video0"
    if len(sys.argv) > 1:
        dev_path = sys.argv[1]

    print("=========================================================")
    print("  GudCam: Ultra-Low-Latency Linux Camera Inspection")
    print("=========================================================")

    # Add cwd to python path
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

    from gudcam.engine import GudCamEngine

    engine = GudCamEngine(lib_path="./libgudcam.so")
    
    # Core 2 affinity for main thread
    engine.configure_render_thread(core2=2, priority50=50)

    # Open device or fallback to synthetic test pattern mode
    opened = engine.open_device(dev_path)
    if not opened:
        print(f"[Launcher] Device {dev_path} not found or busy. Activating Synthetic Pattern Mode...")
        engine.set_synthetic_mode(True)

    engine.set_format(640, 480, 0x56595559, 30) # 640x480 Ultra-Low Latency USB 2.0 YUYV @ 30 FPS
    engine.init_mmap(8)
    
    # Core 1 SCHED_FIFO Priority 80 for ingestion
    engine.start_capture(core1=1, priority80=80)

    try:
        from gudcam.hud import GudCamHUD
        hud = GudCamHUD(engine)
        hud.run()
    except Exception as e:
        print(f"[Launcher] GUI launch failed ({e}). Running in Console Inspection mode...")
        import numpy as np
        buf = np.zeros((1920 * 1080 * 4,), dtype=np.uint8)
        last_t = time.time()
        
        while True:
            has_frame = engine.acquire_frame(buf, sr_mode=2)
            if has_frame and time.time() - last_t >= 1.0:
                print(f"[Console Telemetry] Output: {engine.last_width}x{engine.last_height} (ESPCN x2) | "
                      f"Render FPS: {engine.last_fps:.1f} | Latency: {engine.last_latency_ms:.3f} ms | "
                      f"Seq: #{engine.last_seq} | Drops: {engine.last_drops}")
                last_t = time.time()
            time.sleep(0.001)

    finally:
        print("[Launcher] Stopping capture engine...")
        engine.stop_capture()
        engine.close_device()
        print("[Launcher] Exit clean.")

if __name__ == "__main__":
    main()
