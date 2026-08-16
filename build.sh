#!/usr/bin/env bash
set -e

echo "[gudcam] Building High-Performance Linux Camera Inspection Engine..."
make clean
make -j$(nproc)

echo "[gudcam] Build successful!"
echo "  - Dynamic Library: ./libgudcam.so"
echo "  - Standalone Binary: ./gudcam_app"
echo "  - Lock-Free Test: ./tests/test_lockfree"
echo "  - V4L2 Engine Test: ./tests/test_v4l2"
