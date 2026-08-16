#!/usr/bin/env bash
set -e

echo "=========================================================="
echo "  gudcam Real-Time Linux Kernel Setup & Dependency Script"
echo "=========================================================="

# 1. Real-time scheduling configuration advice
echo "[1/3] Checking real-time SCHED_FIFO rlimit settings..."
if grep -q "@realtime" /etc/security/limits.conf 2>/dev/null || [ -f /etc/security/limits.d/99-realtime.conf ]; then
    echo "  -> Real-time limits already configured in /etc/security/limits!"
else
    echo "  -> NOTE: To allow non-root SCHED_FIFO priority 80, create /etc/security/limits.d/99-realtime.conf with:"
    echo "     @realtime soft rtprio 99"
    echo "     @realtime hard rtprio 99"
    echo "     @realtime soft memlock unlimited"
    echo "     @realtime hard memlock unlimited"
    echo "     (And add user to realtime group: sudo usermod -aG realtime \$USER)"
fi

# 2. Check video device group permissions
echo "[2/3] Checking /dev/video* permissions..."
if [ -c /dev/video0 ]; then
    echo "  -> /dev/video0 detected!"
    ls -l /dev/video0
else
    echo "  -> No physical camera /dev/video0 found. Synthetic Pattern Mode will be used automatically."
fi

# 3. Grant execute permissions to build scripts
chmod +x build.sh main.py 2>/dev/null || true

echo "Setup complete! Run ./build.sh to compile."
