# GudCam: Ultra-Low-Latency Inspection Suite

GudCam is a hyper-optimized, real-time Linux camera inspection engine designed for ultra-low latency digital microscope and webcam feeds. It bypasses traditional software bottlenecks by leveraging lock-free atomic triple-buffering, direct `v4l2` hardware polling, and C++ OpenMP multi-threading decoupled completely from the Python UI.

## What It Is Meant To Do
- **Ultra-Low Latency Feed**: Provide a sub-millisecond, zero-copy video feed directly from your hardware sensor to PyOpenGL VRAM.
- **Real-Time Super-Resolution**: Upscale low-resolution streams (e.g., 640x480) into pristine 1080p/1440p using various scaling kernels (Bilinear, Bicubic, AI ESPCN, AMD FSR 4 approximations) in real-time.
- **Hardware-Level Tweaking**: Expose underlying raw `v4l2` controls (Exposure, Gain, Focus, White Balance) dynamically.
- **Industrial Inspection**: Perfect for soldering, PCB inspection, or precise mechanical tasks where visual latency ruins hand-eye coordination. Features highly aggressive optical image stabilization (OIS) and target reticles.

## What It Is NOT Meant To Do
- **Photography/Video Recording**: GudCam is purely a live viewing engine. It does not currently record video or take snapshots.
- **Cross-Platform Compatibility**: This is tightly coupled to Linux and the `v4l2` (Video4Linux2) subsystem. It will not work on Windows or macOS natively.
- **GPU Compute Processing (Yet)**: *Note on AMD GPUs:* Currently, the complex Super-Resolution and Frame Generation math (like FSR 4) is highly optimized to run on your host CPU via OpenMP multi-threading. It is not currently using hardware-accelerated GPU GLSL compute shaders.

## Can I run this with an AMD 9000 Series GPU?
**Yes**, but your GPU won't be doing the heavy lifting for the FSR calculations. Because the pipeline relies on CPU-bound OpenMP multi-threading, your framerate will scale based on your CPU core count and single-thread performance, rather than your GPU. The PyOpenGL renderer will leverage the GPU for drawing the UI, but the pixel math is strictly CPU. Porting the scaling pipeline to native OpenGL Compute Shaders for full GPU utilization is planned for the future!

## Architecture
- **C++ Native Engine (`libgudcam.so`)**: Handles all raw `v4l2` edge-triggered `epoll` ingestion, zero-copy conversion, and pixel crunching.
- **Lock-Free Pipeline**: A custom atomic triple-buffer system eliminates mutexes, guaranteeing that the ingestion thread and the render thread never block one another.
- **Python GUI**: A lightweight `hello_imgui` and PyOpenGL frontend that acts purely as a dumb window, avoiding the Python Global Interpreter Lock (GIL) entirely during mathematical operations.

## Build Instructions
Ensure you have `g++`, `make`, `v4l-utils`, and python dependencies (`hello_imgui`, `PyOpenGL`, `numpy`) installed.

```bash
./build.sh
DISPLAY=:0 python3 main.py /dev/video0
```
