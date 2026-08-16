import sys, os
from imgui_bundle import hello_imgui
import OpenGL.GL as gl

def show_gl():
    print("glDispatchCompute valid?", bool(gl.glDispatchCompute))
    print("glBindImageTexture from gl valid?", bool(gl.glBindImageTexture))
    print("glMemoryBarrier from gl valid?", bool(gl.glMemoryBarrier))
    hello_imgui.get_runner_params().app_shall_exit = True

params = hello_imgui.RunnerParams()
params.callbacks.show_gui = show_gl
hello_imgui.run(params)
