import os
import ctypes
import json
import numpy as np

class GudCamEngine:
    def __init__(self, lib_path="./libgudcam.so"):
        if not os.path.exists(lib_path):
            alt_path = os.path.join(os.path.dirname(__file__), "..", "build", "libgudcam.so")
            if os.path.exists(alt_path):
                lib_path = alt_path

        self._lib = ctypes.CDLL(lib_path)
        self._setup_c_types()

        self._ctx = self._lib.gudcam_create_context()
        if not self._ctx:
            raise RuntimeError("Failed to create GudCam C++ Context")

        self.last_width = 0
        self.last_height = 0
        self.last_seq = 0
        self.last_latency_ms = 0.0
        self.last_fps = 0.0
        self.last_drops = 0
        self.last_focus_score = 0.0

    def _setup_c_types(self):
        self._lib.gudcam_create_context.restype = ctypes.c_void_p
        self._lib.gudcam_destroy_context.argtypes = [ctypes.c_void_p]
        
        self._lib.gudcam_open_device.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        self._lib.gudcam_open_device.restype = ctypes.c_bool

        self._lib.gudcam_close_device.argtypes = [ctypes.c_void_p]

        self._lib.gudcam_set_synthetic.argtypes = [ctypes.c_void_p, ctypes.c_bool]

        self._lib.gudcam_set_format.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32]
        self._lib.gudcam_set_format.restype = ctypes.c_bool

        self._lib.gudcam_init_mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self._lib.gudcam_init_mmap.restype = ctypes.c_bool

        self._lib.gudcam_start_capture.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        self._lib.gudcam_start_capture.restype = ctypes.c_bool

        self._lib.gudcam_stop_capture.argtypes = [ctypes.c_void_p]

        self._lib.gudcam_configure_render_thread.argtypes = [ctypes.c_int, ctypes.c_int]
        self._lib.gudcam_configure_render_thread.restype = ctypes.c_bool

        self._lib.gudcam_acquire_processed_frame.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint32),
            ctypes.POINTER(ctypes.c_uint32),
            ctypes.POINTER(ctypes.c_uint64),
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_double),
            ctypes.c_bool
        ]
        self._lib.gudcam_acquire_processed_frame.restype = ctypes.c_bool

        self._lib.gudcam_query_controls_json.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
        self._lib.gudcam_query_controls_json.restype = ctypes.c_int

        self._lib.gudcam_set_control.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int32]
        self._lib.gudcam_set_control.restype = ctypes.c_bool

        self._lib.gudcam_set_microscope_params.argtypes = [
            ctypes.c_void_p,
            ctypes.c_float,
            ctypes.c_float,
            ctypes.c_float,
            ctypes.c_float,
            ctypes.c_bool,
            ctypes.c_bool,
            ctypes.c_bool,
            ctypes.c_bool,
            ctypes.c_bool,
            ctypes.c_bool,
            ctypes.c_bool
        ]

    def open_device(self, dev_path="/dev/video0"):
        return self._lib.gudcam_open_device(self._ctx, dev_path.encode('utf-8'))

    def close_device(self):
        self._lib.gudcam_close_device(self._ctx)

    def set_synthetic_mode(self, enabled=True):
        self._lib.gudcam_set_synthetic(self._ctx, enabled)

    def set_format(self, width=640, height=480, fourcc=0x56595559, fps=30): # 0x56595559 = YUYV
        return self._lib.gudcam_set_format(self._ctx, width, height, fourcc, fps)

    def init_mmap(self, buffer_count=8):
        return self._lib.gudcam_init_mmap(self._ctx, buffer_count)

    def start_capture(self, core1=1, priority80=80):
        return self._lib.gudcam_start_capture(self._ctx, core1, priority80)

    def stop_capture(self):
        self._lib.gudcam_stop_capture(self._ctx)

    def configure_render_thread(self, core2=2, priority50=50):
        return self._lib.gudcam_configure_render_thread(core2, priority50)

    def acquire_frame(self, target_buf, sr_mode=0, hw_accel=True):
        out_w = ctypes.c_uint32(0)
        out_h = ctypes.c_uint32(0)
        out_seq = ctypes.c_uint64(0)
        out_lat = ctypes.c_double(0.0)
        out_fps = ctypes.c_double(0.0)
        out_drops = ctypes.c_uint64(0)
        out_focus = ctypes.c_double(0.0)

        c_buf = target_buf.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        has_new = self._lib.gudcam_acquire_processed_frame(
            self._ctx,
            c_buf,
            target_buf.nbytes,
            ctypes.byref(out_w),
            ctypes.byref(out_h),
            ctypes.byref(out_seq),
            sr_mode,
            ctypes.byref(out_lat),
            ctypes.byref(out_fps),
            ctypes.byref(out_drops),
            ctypes.byref(out_focus),
            hw_accel
        )

        self.last_width = out_w.value
        self.last_height = out_h.value
        self.last_seq = out_seq.value
        self.last_latency_ms = out_lat.value
        self.last_fps = out_fps.value
        self.last_drops = out_drops.value
        self.last_focus_score = out_focus.value

        return has_new

    def get_controls(self):
        buf = ctypes.create_string_buffer(65536)
        res = self._lib.gudcam_query_controls_json(self._ctx, buf, 65536)
        if res > 0:
            try:
                return json.loads(buf.value.decode('utf-8'))
            except Exception as e:
                print(f"[Python Engine] Error parsing controls JSON: {e}")
        return []

    def set_control(self, ctrl_id, value):
        return self._lib.gudcam_set_control(self._ctx, ctrl_id, value)

    def set_microscope_params(self, zoom_factor=1.0, pan_x=0.0, pan_y=0.0, focus_offset=0.0, focus_peaking=False, hdr_boost=False, optical_boost=True, ar_reconstruction=True, stabilization_enabled=True, temporal_fusion=True, frame_generation=True):
        self._lib.gudcam_set_microscope_params(self._ctx, float(zoom_factor), float(pan_x), float(pan_y), float(focus_offset), bool(focus_peaking), bool(hdr_boost), bool(optical_boost), bool(ar_reconstruction), bool(stabilization_enabled), bool(temporal_fusion), bool(frame_generation))

    def __del__(self):
        if hasattr(self, '_ctx') and self._ctx:
            self._lib.gudcam_destroy_context(self._ctx)
            self._ctx = None
