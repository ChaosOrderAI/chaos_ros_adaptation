"""Depth-aware Gaussian surface splatting of measured RGB points.

This renderer uses estimated surface normals and Gaussian color weights. It is
not a photo-trained 3DGS model. All source points share the viewer's existing VBOs.
"""
import numpy as np
from OpenGL import GL
from OpenGL.GL.shaders import compileProgram, compileShader


GEOMETRY = """#version 330 compatibility
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in VertexData {
    vec3 center;
    vec3 normal;
    vec4 color;
    float radius;
} vertices[];
uniform mat4 projection;
uniform float radius_scale;
uniform float framebuffer_height;
out vec2 splat_uv;
out vec4 point_color;
flat out float depth_band;
void main() {
    vec3 center = vertices[0].center;
    vec3 normal = vertices[0].normal;
    float measured_radius = vertices[0].radius * radius_scale;
    // Gaussian support extends past the measured sampling footprint. A small
    // pixel footprint filters distant points without changing source density.
    float radius = max(1.25 * measured_radius,
                       -2.0 * center.z / (framebuffer_height * abs(projection[1][1])));
    vec4 clip_center = projection * vec4(center, 1.0);
    if (clip_center.w < -radius ||
        abs(clip_center.x) > clip_center.w + radius * (abs(projection[0][0]) + 1.0) ||
        abs(clip_center.y) > clip_center.w + radius * (abs(projection[1][1]) + 1.0)) return;
    vec3 reference = abs(normal.z) < 0.9 ? vec3(0, 0, 1) : vec3(0, 1, 0);
    vec3 tangent = normalize(cross(normal, reference));
    vec3 bitangent = cross(normal, tangent);
    for (int i = 0; i < 4; ++i) {
        vec2 corner = vec2((i & 1) == 0 ? -1.0 : 1.0, i < 2 ? -1.0 : 1.0);
        vec3 position = center + radius * (corner.x * tangent + corner.y * bitangent);
        gl_Position = projection * vec4(position, 1.0);
        splat_uv = corner;
        point_color = vertices[0].color;
        depth_band = clamp(0.5 * measured_radius, 0.008, 0.035);
        EmitVertex();
    }
    EndPrimitive();
}
"""

DEPTH_FRAGMENT = """#version 330 compatibility
in vec2 splat_uv;
void main() {
    if (dot(splat_uv, splat_uv) >= 1.0) discard;
}
"""

ACCUMULATE_FRAGMENT = """#version 330 compatibility
in vec2 splat_uv;
in vec4 point_color;
flat in float depth_band;
uniform sampler2D surface_depth;
uniform mat4 projection;
uniform bool zero_to_one;
out vec4 accumulated;
float eye_distance(float depth) {
    float ndc = zero_to_one ? depth : 2.0 * depth - 1.0;
    return projection[3][2] / (ndc + projection[2][2]);
}
void main() {
    float r2 = dot(splat_uv, splat_uv);
    if (r2 >= 1.0) discard;
    float nearest = texelFetch(surface_depth, ivec2(gl_FragCoord.xy), 0).r;
    float delta = max(0.0, eye_distance(gl_FragCoord.z) - eye_distance(nearest));
    if (delta > depth_band) discard;
    float weight = exp(-3.0 * r2) * exp(-2.0 * pow(delta / depth_band, 2.0));
    vec3 linear_rgb = pow(clamp(point_color.rgb, 0.0, 1.0), vec3(2.2));
    accumulated = vec4(linear_rgb * weight, weight);
}
"""

RESOLVE_VERTEX = """#version 330 compatibility
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
"""

RESOLVE_FRAGMENT = """#version 330 compatibility
uniform sampler2D surface_depth;
uniform sampler2D accumulated_color;
out vec4 color;
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 sum = texelFetch(accumulated_color, pixel, 0);
    if (sum.a < 0.0001) discard;
    vec3 rgb = pow(max(sum.rgb / sum.a, vec3(0.0)), vec3(1.0 / 2.2));
    // Normalize the opaque measured surface. Fading this nearest layer into
    // the sky would create false holes/halos between overlapping plant leaves.
    color = vec4(rgb, 1.0);
    // The original MuJoCo MSAA depth remains in the destination framebuffer.
    // This strict per-sample test keeps the robot visible in front of the cloud.
    gl_FragDepth = texelFetch(surface_depth, pixel, 0).r;
}
"""


def program(vertex, fragment, geometry=None):
    shaders = [compileShader(vertex, GL.GL_VERTEX_SHADER)]
    if geometry:
        shaders.append(compileShader(geometry, GL.GL_GEOMETRY_SHADER))
    shaders.append(compileShader(fragment, GL.GL_FRAGMENT_SHADER))
    return compileProgram(*shaders)


class GaussianSurfaceRenderer:
    def __init__(self, points, vertex_shader):
        if not points.oriented:
            raise ValueError('Gaussian surface rendering requires point normals')
        self.points = points
        self.depth_program = program(vertex_shader, DEPTH_FRAGMENT, GEOMETRY)
        self.accum_program = program(vertex_shader, ACCUMULATE_FRAGMENT, GEOMETRY)
        self.resolve_program = program(RESOLVE_VERTEX, RESOLVE_FRAGMENT)
        self.uniforms = {p: {name: GL.glGetUniformLocation(p, name) for name in
                            ('projection', 'view', 'radius_scale', 'framebuffer_height',
                             'surface_depth', 'accumulated_color', 'zero_to_one')}
                         for p in (self.depth_program, self.accum_program, self.resolve_program)}
        self.depth_fbo, self.accum_fbo = [int(x) for x in GL.glGenFramebuffers(2)]
        self.depth_texture, self.accum_texture = [int(x) for x in GL.glGenTextures(2)]
        self.empty_vao = int(GL.glGenVertexArrays(1))
        self.size = None
        version = (int(GL.glGetIntegerv(GL.GL_MAJOR_VERSION)), int(GL.glGetIntegerv(GL.GL_MINOR_VERSION)))
        self.clip_control = version >= (4, 5) or b'GL_ARB_clip_control' in (GL.glGetString(GL.GL_EXTENSIONS) or b'')

    def resize(self, width, height):
        if self.size == (width, height):
            return
        for texture, internal, channels, kind in (
                (self.depth_texture, GL.GL_DEPTH_COMPONENT32F, GL.GL_DEPTH_COMPONENT, GL.GL_FLOAT),
                (self.accum_texture, GL.GL_RGBA16F, GL.GL_RGBA, GL.GL_HALF_FLOAT)):
            GL.glBindTexture(GL.GL_TEXTURE_2D, texture)
            GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, internal, width, height, 0, channels, kind, None)
            GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_NEAREST)
            GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_NEAREST)
            GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_S, GL.GL_CLAMP_TO_EDGE)
            GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_T, GL.GL_CLAMP_TO_EDGE)
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.depth_fbo)
        GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER, GL.GL_DEPTH_ATTACHMENT,
                                 GL.GL_TEXTURE_2D, self.depth_texture, 0)
        GL.glDrawBuffer(GL.GL_NONE)
        GL.glReadBuffer(GL.GL_NONE)
        self.check_framebuffer()
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.accum_fbo)
        GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER, GL.GL_COLOR_ATTACHMENT0,
                                 GL.GL_TEXTURE_2D, self.accum_texture, 0)
        GL.glDrawBuffer(GL.GL_COLOR_ATTACHMENT0)
        GL.glReadBuffer(GL.GL_COLOR_ATTACHMENT0)
        self.check_framebuffer()
        self.size = (width, height)

    @staticmethod
    def check_framebuffer():
        status = GL.glCheckFramebufferStatus(GL.GL_FRAMEBUFFER)
        if status != GL.GL_FRAMEBUFFER_COMPLETE:
            raise RuntimeError(f'Gaussian framebuffer incomplete: 0x{status:x}')

    def draw_points(self, shader, projection, view, radius, height):
        GL.glUseProgram(shader)
        u = self.uniforms[shader]
        GL.glUniformMatrix4fv(u['projection'], 1, GL.GL_FALSE, projection)
        GL.glUniformMatrix4fv(u['view'], 1, GL.GL_FALSE, view)
        GL.glUniform1f(u['radius_scale'], radius / self.points.radius_reference)
        GL.glUniform1f(u['framebuffer_height'], height)
        GL.glBindVertexArray(self.points.vao)
        if self.points.radius_vbo is None:
            GL.glVertexAttrib1f(3, self.points.radius_reference)
        GL.glDrawArrays(GL.GL_POINTS, 0, self.points.count)

    def render(self, height, radius):
        projection = GL.glGetFloatv(GL.GL_PROJECTION_MATRIX)
        view = GL.glGetFloatv(GL.GL_MODELVIEW_MATRIX)
        viewport = GL.glGetIntegerv(GL.GL_VIEWPORT)
        width = int(viewport[2])
        if tuple(viewport[:2]) != (0, 0) or int(viewport[3]) != height:
            raise ValueError('Gaussian renderer expects a full-window viewport')
        original_draw = int(GL.glGetIntegerv(GL.GL_DRAW_FRAMEBUFFER_BINDING))
        original_read = int(GL.glGetIntegerv(GL.GL_READ_FRAMEBUFFER_BINDING))
        original_program = int(GL.glGetIntegerv(GL.GL_CURRENT_PROGRAM))
        original_vao = int(GL.glGetIntegerv(GL.GL_VERTEX_ARRAY_BINDING))
        original_active = int(GL.glGetIntegerv(GL.GL_ACTIVE_TEXTURE))
        bindings = []
        for unit in (GL.GL_TEXTURE0, GL.GL_TEXTURE1):
            GL.glActiveTexture(unit)
            bindings.append(int(GL.glGetIntegerv(GL.GL_TEXTURE_BINDING_2D)))
        GL.glActiveTexture(original_active)
        GL.glPushAttrib(GL.GL_ALL_ATTRIB_BITS)
        try:
            GL.glActiveTexture(GL.GL_TEXTURE0)
            self.resize(width, height)
            GL.glViewport(0, 0, width, height)
            for capability in (GL.GL_LIGHTING, GL.GL_FOG, GL.GL_CULL_FACE,
                               GL.GL_ALPHA_TEST, GL.GL_SCISSOR_TEST, GL.GL_STENCIL_TEST,
                               GL.GL_SAMPLE_ALPHA_TO_COVERAGE, GL.GL_BLEND, GL.GL_MULTISAMPLE):
                GL.glDisable(capability)
            GL.glDisable(GL.GL_FRAMEBUFFER_SRGB)
            GL.glDepthRange(0., 1.)
            GL.glDepthFunc(GL.GL_GEQUAL)
            GL.glEnable(GL.GL_DEPTH_TEST)
            GL.glDepthMask(GL.GL_TRUE)
            GL.glColorMask(GL.GL_FALSE, GL.GL_FALSE, GL.GL_FALSE, GL.GL_FALSE)
            GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.depth_fbo)
            GL.glClearDepth(0.)
            GL.glClear(GL.GL_DEPTH_BUFFER_BIT)
            self.draw_points(self.depth_program, projection, view, radius, height)

            GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.accum_fbo)
            GL.glColorMask(GL.GL_TRUE, GL.GL_TRUE, GL.GL_TRUE, GL.GL_TRUE)
            GL.glDisable(GL.GL_DEPTH_TEST)
            GL.glDepthMask(GL.GL_FALSE)
            GL.glClearColor(0., 0., 0., 0.)
            GL.glClear(GL.GL_COLOR_BUFFER_BIT)
            GL.glEnable(GL.GL_BLEND)
            GL.glBlendEquation(GL.GL_FUNC_ADD)
            GL.glBlendFunc(GL.GL_ONE, GL.GL_ONE)
            GL.glActiveTexture(GL.GL_TEXTURE0)
            GL.glBindTexture(GL.GL_TEXTURE_2D, self.depth_texture)
            GL.glUseProgram(self.accum_program)
            GL.glUniform1i(self.uniforms[self.accum_program]['surface_depth'], 0)
            # PyOpenGL's size table does not include this newer scalar enum.
            depth_mode = np.zeros(1, dtype=np.int32)
            if self.clip_control:
                GL.glGetIntegerv(GL.GL_CLIP_DEPTH_MODE, depth_mode)
            zero_to_one = int(depth_mode[0]) == GL.GL_ZERO_TO_ONE
            GL.glUniform1i(self.uniforms[self.accum_program]['zero_to_one'], zero_to_one)
            self.draw_points(self.accum_program, projection, view, radius, height)

            GL.glBindFramebuffer(GL.GL_DRAW_FRAMEBUFFER, original_draw)
            GL.glBindFramebuffer(GL.GL_READ_FRAMEBUFFER, original_read)
            GL.glEnable(GL.GL_MULTISAMPLE)
            GL.glEnable(GL.GL_DEPTH_TEST)
            GL.glDepthMask(GL.GL_FALSE)
            GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)
            GL.glUseProgram(self.resolve_program)
            GL.glUniform1i(self.uniforms[self.resolve_program]['surface_depth'], 0)
            GL.glActiveTexture(GL.GL_TEXTURE1)
            GL.glBindTexture(GL.GL_TEXTURE_2D, self.accum_texture)
            GL.glUniform1i(self.uniforms[self.resolve_program]['accumulated_color'], 1)
            GL.glBindVertexArray(self.empty_vao)
            GL.glDrawArrays(GL.GL_TRIANGLES, 0, 3)
        finally:
            GL.glBindFramebuffer(GL.GL_DRAW_FRAMEBUFFER, original_draw)
            GL.glBindFramebuffer(GL.GL_READ_FRAMEBUFFER, original_read)
            GL.glPopAttrib()
            GL.glUseProgram(original_program)
            GL.glBindVertexArray(original_vao)
            for unit, binding in zip((GL.GL_TEXTURE0, GL.GL_TEXTURE1), bindings):
                GL.glActiveTexture(unit)
                GL.glBindTexture(GL.GL_TEXTURE_2D, binding)
            GL.glActiveTexture(original_active)

    def close(self):
        GL.glDeleteFramebuffers(2, [self.depth_fbo, self.accum_fbo])
        GL.glDeleteTextures([self.depth_texture, self.accum_texture])
        GL.glDeleteVertexArrays(1, [self.empty_vao])
        for shader in (self.depth_program, self.accum_program, self.resolve_program):
            GL.glDeleteProgram(shader)
