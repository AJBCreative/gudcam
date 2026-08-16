import sys, os
from imgui_bundle import hello_imgui
import OpenGL.GL as gl
from OpenGL.GL.ARB.shader_image_load_store import glBindImageTexture, glMemoryBarrier

def show_gl():
    print("GL Version:", gl.glGetString(gl.GL_VERSION))
    print("glBindImageTexture valid?", bool(glBindImageTexture))
    print("glMemoryBarrier valid?", bool(glMemoryBarrier))
    hello_imgui.get_runner_params().app_shall_exit = True

params = hello_imgui.RunnerParams()
params.callbacks.show_gui = show_gl
hello_imgui.run(params)
