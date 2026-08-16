import OpenGL.GL as gl
from OpenGL.GL import shaders

COMPUTE_SHADER_BILINEAR = """#version 430
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D inputImage;
layout(binding = 1, rgba8) uniform writeonly image2D outputImage;

uniform float zoom_factor;
uniform float pan_x;
uniform float pan_y;

void main() {
    ivec2 out_pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 out_size = imageSize(outputImage);
    ivec2 in_size = imageSize(inputImage);
    
    if (out_pos.x >= out_size.x || out_pos.y >= out_size.y) return;
    
    // Calculate normalized coordinates
    vec2 uv = vec2(out_pos) / vec2(out_size);
    
    // Apply pan and zoom
    uv = (uv - 0.5) / zoom_factor + 0.5;
    uv.x += pan_x;
    uv.y += pan_y;
    
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        imageStore(outputImage, out_pos, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    
    // Bilinear interpolation
    vec2 in_pos = uv * vec2(in_size);
    
    vec4 p00 = imageLoad(inputImage, ivec2(floor(in_pos.x), floor(in_pos.y)));
    vec4 p10 = imageLoad(inputImage, ivec2(ceil(in_pos.x), floor(in_pos.y)));
    vec4 p01 = imageLoad(inputImage, ivec2(floor(in_pos.x), ceil(in_pos.y)));
    vec4 p11 = imageLoad(inputImage, ivec2(ceil(in_pos.x), ceil(in_pos.y)));
    
    vec2 fract_pos = fract(in_pos);
    
    vec4 top = mix(p00, p10, fract_pos.x);
    vec4 bottom = mix(p01, p11, fract_pos.x);
    vec4 result = mix(top, bottom, fract_pos.y);
    
    imageStore(outputImage, out_pos, result);
}
"""

COMPUTE_SHADER_FSR4 = """#version 430
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D inputImage;
layout(binding = 1, rgba8) uniform writeonly image2D outputImage;

uniform float zoom_factor;
uniform float pan_x;
uniform float pan_y;

// Simplified Edge-Adaptive Spatial Upsampling (FSR-like)
void main() {
    ivec2 out_pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 out_size = imageSize(outputImage);
    ivec2 in_size = imageSize(inputImage);
    
    if (out_pos.x >= out_size.x || out_pos.y >= out_size.y) return;
    
    vec2 uv = vec2(out_pos) / vec2(out_size);
    uv = (uv - 0.5) / zoom_factor + 0.5;
    uv.x += pan_x;
    uv.y += pan_y;
    
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        imageStore(outputImage, out_pos, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    
    vec2 in_pos = uv * vec2(in_size);
    ivec2 ipos = ivec2(in_pos);
    
    // Fetch 3x3 neighborhood
    vec4 c00 = imageLoad(inputImage, ipos + ivec2(-1, -1));
    vec4 c10 = imageLoad(inputImage, ipos + ivec2( 0, -1));
    vec4 c20 = imageLoad(inputImage, ipos + ivec2( 1, -1));
    vec4 c01 = imageLoad(inputImage, ipos + ivec2(-1,  0));
    vec4 c11 = imageLoad(inputImage, ipos + ivec2( 0,  0)); // Center
    vec4 c21 = imageLoad(inputImage, ipos + ivec2( 1,  0));
    vec4 c02 = imageLoad(inputImage, ipos + ivec2(-1,  1));
    vec4 c12 = imageLoad(inputImage, ipos + ivec2( 0,  1));
    vec4 c22 = imageLoad(inputImage, ipos + ivec2( 1,  1));
    
    // Simple edge detection (Sobel-like magnitude)
    float edge_h = length((c20 + 2.0*c21 + c22) - (c00 + 2.0*c01 + c02));
    float edge_v = length((c02 + 2.0*c12 + c22) - (c00 + 2.0*c10 + c20));
    
    vec2 f = fract(in_pos);
    vec4 result;
    
    if (edge_h > edge_v) {
        // Vertical edges dominate, interpolate horizontally
        vec4 top = mix(c00, c20, f.x);
        vec4 mid = mix(c01, c21, f.x);
        vec4 bot = mix(c02, c22, f.x);
        // Then vertically
        result = mix(mix(top, mid, f.y), mix(mid, bot, f.y), f.y);
    } else {
        // Horizontal edges dominate, interpolate vertically
        vec4 left  = mix(c00, c02, f.y);
        vec4 mid   = mix(c10, c12, f.y);
        vec4 right = mix(c20, c22, f.y);
        // Then horizontally
        result = mix(mix(left, mid, f.x), mix(mid, right, f.x), f.x);
    }
    
    // Sharpening pass
    vec4 laplacian = 4.0 * c11 - c01 - c21 - c10 - c12;
    result = result + 0.1 * laplacian;
    
    imageStore(outputImage, out_pos, clamp(result, 0.0, 1.0));
}
"""

def compile_compute_shader(source):
    shader = gl.glCreateShader(gl.GL_COMPUTE_SHADER)
    gl.glShaderSource(shader, source)
    gl.glCompileShader(shader)
    if not gl.glGetShaderiv(shader, gl.GL_COMPILE_STATUS):
        print(f"Compute shader compilation failed: {gl.glGetShaderInfoLog(shader).decode()}")
        return None
        
    program = gl.glCreateProgram()
    gl.glAttachShader(program, shader)
    gl.glLinkProgram(program)
    if not gl.glGetProgramiv(program, gl.GL_LINK_STATUS):
        print(f"Compute program linking failed: {gl.glGetProgramInfoLog(program).decode()}")
        return None
        
    gl.glDeleteShader(shader)
    return program
