import ctypes
glx = ctypes.CDLL('libGL.so.1')
glx.glXGetProcAddress.restype = ctypes.c_void_p
glx.glXGetProcAddress.argtypes = [ctypes.c_char_p]
addr = glx.glXGetProcAddress(b"glBindImageTexture")
print("glBindImageTexture addr:", addr)
