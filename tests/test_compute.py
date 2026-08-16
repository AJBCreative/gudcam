import sys, os
sys.path.insert(0, os.path.abspath('.'))
from gudcam.shaders import compile_compute_shader, COMPUTE_SHADER_FSR4
import OpenGL.GL as gl
from OpenGL.GLUT import *

glutInit()
glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH)
glutInitWindowSize(100, 100)
glutCreateWindow(b"Test")

prog = compile_compute_shader(COMPUTE_SHADER_FSR4)
if prog:
    print("Shader compiled successfully!")
else:
    print("Shader failed!")
