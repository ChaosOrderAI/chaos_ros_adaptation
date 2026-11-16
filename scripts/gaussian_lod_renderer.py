"""Level-of-detail renderer: 3D Gaussians far away, measured points up close.

Distant chunks are drawn as the anisotropic Gaussians fitted by
prepare_3d_gaussians.py. Their screen ellipse is the EWA projection of the
3D covariance, so a surface stays covered as the view becomes grazing. A chunk
whose samples would cover more than a few pixels is drawn with the original
points instead. There is no photo training and no per-frame sort.
"""
import ctypes
from pathlib import Path

import numpy as np
from OpenGL import GL

from gaussian_surface_renderer import (
    ACCUMULATE_FRAGMENT, DEPTH_FRAGMENT, GEOMETRY, RESOLVE_FRAGMENT, RESOLVE_VERTEX, program)


GAUSSIAN_VERTEX = """#version 330 compatibility
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec3 covariance_a;
layout(location = 3) in vec3 covariance_b;
uniform mat4 view;
out GaussianData {
    vec3 center;
    vec3 covariance_a;
    vec3 covariance_b;
    vec4 color;
} vertex;
void main() {
    vec4 eye = view * vec4(position, 1.0);
    gl_Position = eye;
    mat3 rotation = mat3(view);
    mat3 covariance = mat3(covariance_a.x, covariance_a.y, covariance_a.z,
                           covariance_a.y, covariance_b.x, covariance_b.y,
                           covariance_a.z, covariance_b.y, covariance_b.z);
    mat3 camera = rotation * covariance * transpose(rotation);
    vertex.center = eye.xyz;
    vertex.covariance_a = vec3(camera[0][0], camera[0][1], camera[0][2]);
    vertex.covariance_b = vec3(camera[1][1], camera[1][2], camera[2][2]);
    vertex.color = color;
}
"""

GAUSSIAN_GEOMETRY = """#version 330 compatibility
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in GaussianData {
    vec3 center;
    vec3 covariance_a;
    vec3 covariance_b;
    vec4 color;
} vertices[];
uniform mat4 projection;
uniform vec2 framebuffer_size;
out vec2 pixel_offset;
flat out vec3 conic;
flat out vec4 point_color;
flat out float depth_band;
void main() {
    vec3 center = vertices[0].center;
    float depth = -center.z;
    if (depth < 0.8) return;
    vec3 a = vertices[0].covariance_a;
    vec3 b = vertices[0].covariance_b;
    float fx = projection[0][0] / depth;
    float fy = projection[1][1] / depth;
    float jxz = -fx * center.x / depth;
    float jyz = -fy * center.y / depth;
    float c00 = fx * fx * a.x + 2.0 * fx * jxz * a.z + jxz * jxz * b.z;
    float c01 = fx * fy * a.y + fx * jyz * a.z + fy * jxz * b.y + jxz * jyz * b.z;
    float c11 = fy * fy * b.x + 2.0 * fy * jyz * b.y + jyz * jyz * b.z;
    vec2 pixel_scale = 0.5 * framebuffer_size;
    c00 *= pixel_scale.x * pixel_scale.x;
    c01 *= pixel_scale.x * pixel_scale.y;
    c11 *= pixel_scale.y * pixel_scale.y;
    float determinant = c00 * c11 - c01 * c01;
    if (determinant <= 1e-6) return;
    // A square of the major-axis radius covers the ellipse whatever its rotation;
    // the conic test discards the corners.
    float mid = 0.5 * (c00 + c11);
    // A Gaussian this close is replaced by the measured points; never let one
    // ellipse cover the window.
    float radius = min(96.0, 3.0 * sqrt(max(1.0, mid + sqrt(max(0.0, mid * mid - determinant)))));
    vec2 extent = vec2(radius);
    vec4 clip = projection * vec4(center, 1.0);
    vec2 ndc = clip.xy / clip.w;
    vec2 pixel = (ndc * 0.5 + 0.5) * framebuffer_size;
    if (pixel.x < -extent.x || pixel.y < -extent.y ||
        pixel.x > framebuffer_size.x + extent.x || pixel.y > framebuffer_size.y + extent.y) return;
    conic = vec3(c11, -c01, c00) / determinant;
    point_color = vertices[0].color;
    depth_band = clamp(sqrt(max(1e-8, min(a.x, min(b.x, b.z)))), 0.02, 0.12);
    for (int i = 0; i < 4; ++i) {
        vec2 corner = vec2((i & 1) == 0 ? -extent.x : extent.x, i < 2 ? -extent.y : extent.y);
        vec2 placed = pixel + corner;
        vec2 placed_ndc = placed / framebuffer_size * 2.0 - 1.0;
        gl_Position = vec4(placed_ndc * clip.w, clip.z, clip.w);
        pixel_offset = corner;
        EmitVertex();
    }
    EndPrimitive();
}
"""

GAUSSIAN_DEPTH_FRAGMENT = """#version 330 compatibility
in vec2 pixel_offset;
flat in vec3 conic;
void main() {
    float power = -0.5 * (conic.x * pixel_offset.x * pixel_offset.x
                          + conic.z * pixel_offset.y * pixel_offset.y
                          + 2.0 * conic.y * pixel_offset.x * pixel_offset.y);
    if (power < -4.5) discard;
}
"""

GAUSSIAN_FRAGMENT = """#version 330 compatibility
in vec2 pixel_offset;
flat in vec3 conic;
flat in vec4 point_color;
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
    float power = -0.5 * (conic.x * pixel_offset.x * pixel_offset.x
                          + conic.z * pixel_offset.y * pixel_offset.y
                          + 2.0 * conic.y * pixel_offset.x * pixel_offset.y);
    if (power < -4.5) discard;
    float nearest = texelFetch(surface_depth, ivec2(gl_FragCoord.xy), 0).r;
    float delta = max(0.0, eye_distance(gl_FragCoord.z) - eye_distance(nearest));
    if (delta > depth_band) discard;
    float weight = exp(power) * exp(-2.0 * pow(delta / depth_band, 2.0));
    vec3 linear_rgb = pow(clamp(point_color.rgb, 0.0, 1.0), vec3(2.2));
    accumulated = vec4(linear_rgb * weight, weight);
}
"""


class GaussianLodRenderer:
    def __init__(self, points, vertex_shader, directory):
        directory = Path(directory)
        self.points = points
        self.gaussians = np.load(directory / 'gaussians.npy', mmap_mode='r', allow_pickle=False)
        self.chunks = np.load(directory / 'gaussian_chunks.npy', mmap_mode='r', allow_pickle=False)
        self.index = np.load(directory / 'cloud_chunk_index.npy', mmap_mode='r', allow_pickle=False)
        if self.index.shape != (points.count,) or self.index.dtype != np.uint32:
            raise ValueError('Chunk index does not match the loaded point cloud')
        self.point_depth = program(vertex_shader, DEPTH_FRAGMENT, GEOMETRY)
        self.point_accum = program(vertex_shader, ACCUMULATE_FRAGMENT, GEOMETRY)
        self.gaussian_depth = program(GAUSSIAN_VERTEX, GAUSSIAN_DEPTH_FRAGMENT, GAUSSIAN_GEOMETRY)
        self.gaussian_accum = program(GAUSSIAN_VERTEX, GAUSSIAN_FRAGMENT, GAUSSIAN_GEOMETRY)
        self.resolve_program = program(RESOLVE_VERTEX, RESOLVE_FRAGMENT)
        names = ('projection', 'view', 'radius_scale', 'framebuffer_height', 'framebuffer_size',
                 'surface_depth', 'accumulated_color', 'zero_to_one')
        shaders = (self.point_depth, self.point_accum, self.gaussian_depth,
                   self.gaussian_accum, self.resolve_program)
        self.uniforms = {shader: {name: GL.glGetUniformLocation(shader, name) for name in names}
                         for shader in shaders}
        self.depth_fbo, self.accum_fbo = [int(value) for value in GL.glGenFramebuffers(2)]
        self.depth_texture, self.accum_texture = [int(value) for value in GL.glGenTextures(2)]
        self.empty_vao = int(GL.glGenVertexArrays(1))
        self.point_vao = self._point_vao()
        self.gaussian_vao, self.gaussian_vbo = self._gaussian_vao()
        self.size = None
        version = (int(GL.glGetIntegerv(GL.GL_MAJOR_VERSION)), int(GL.glGetIntegerv(GL.GL_MINOR_VERSION)))
        self.clip_control = version >= (4, 5) or b'GL_ARB_clip_control' in (GL.glGetString(GL.GL_EXTENSIONS) or b'')
        self.label = f'3DGS {len(self.gaussians) / 1e6:.2f}M/{points.count / 1e6:.2f}M'

    def _point_vao(self):
        vao = int(GL.glGenVertexArrays(1))
        GL.glBindVertexArray(vao)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.points.vbo)
        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 3, GL.GL_FLOAT, GL.GL_FALSE, 16, ctypes.c_void_p(0))
        GL.glEnableVertexAttribArray(1)
        GL.glVertexAttribPointer(1, 4, GL.GL_UNSIGNED_BYTE, GL.GL_TRUE, 16, ctypes.c_void_p(12))
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.points.normal_vbo)
        GL.glEnableVertexAttribArray(2)
        GL.glVertexAttribPointer(2, 3, GL.GL_FLOAT, GL.GL_FALSE, 12, ctypes.c_void_p(0))
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self.points.radius_vbo)
        GL.glEnableVertexAttribArray(3)
        GL.glVertexAttribPointer(3, 1, GL.GL_FLOAT, GL.GL_FALSE, 4, ctypes.c_void_p(0))
        element = int(GL.glGenBuffers(1))
        GL.glBindBuffer(GL.GL_ELEMENT_ARRAY_BUFFER, element)
        GL.glBufferData(GL.GL_ELEMENT_ARRAY_BUFFER, self.index.nbytes, self.index, GL.GL_STATIC_DRAW)
        GL.glBindVertexArray(0)
        self.point_element = element
        return vao

    def _gaussian_vao(self):
        vao = int(GL.glGenVertexArrays(1))
        vbo = int(GL.glGenBuffers(1))
        GL.glBindVertexArray(vao)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, vbo)
        GL.glBufferData(GL.GL_ARRAY_BUFFER, self.gaussians.nbytes, self.gaussians, GL.GL_STATIC_DRAW)
        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 3, GL.GL_FLOAT, GL.GL_FALSE, 40, ctypes.c_void_p(0))
        GL.glEnableVertexAttribArray(1)
        GL.glVertexAttribPointer(1, 4, GL.GL_UNSIGNED_BYTE, GL.GL_TRUE, 40, ctypes.c_void_p(12))
        GL.glEnableVertexAttribArray(2)
        GL.glVertexAttribPointer(2, 3, GL.GL_FLOAT, GL.GL_FALSE, 40, ctypes.c_void_p(16))
        GL.glEnableVertexAttribArray(3)
        GL.glVertexAttribPointer(3, 3, GL.GL_FLOAT, GL.GL_FALSE, 40, ctypes.c_void_p(28))
        GL.glBindVertexArray(0)
        return vao, vbo

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

    def ranges(self, projection, view, height):
        """Chunks whose 4 cm samples cover at least 2.5 pixels use the original points."""
        def matrix(value):
            return np.array(value, np.float64).reshape(-1)[:16].reshape(4, 4, order='F')

        view_matrix = matrix(view)
        camera = -view_matrix[:3, :3].T @ view_matrix[:3, 3]
        centers = np.asarray(self.chunks['center'], np.float64)
        # Distance to the chunk centre, not its bounding radius: a tall building
        # would otherwise pull the whole chunk into the expensive point path.
        distance = np.maximum(np.linalg.norm(centers - camera, axis=1), 0.3)
        focal = 0.5 * height * abs(float(matrix(projection)[1, 1]))
        near = (0.04 * focal) / distance >= 2.5
        if not getattr(self, 'reported', False):
            print(f'3DGS LOD camera {camera.round(1).tolist()}: '
                  f'{int(near.sum())} point chunks, {int((~near).sum())} Gaussian chunks', flush=True)
            self.reported = True
        return self._runs(near), self._runs(~near)

    @staticmethod
    def _runs(mask):
        if not np.any(mask):
            return []
        edges = np.flatnonzero(np.diff(np.r_[False, mask, False]))
        return list(zip(edges[0::2], edges[1::2]))

    def draw_points(self, shader, runs, projection, view, radius, height):
        GL.glUseProgram(shader)
        uniform = self.uniforms[shader]
        GL.glUniformMatrix4fv(uniform['projection'], 1, GL.GL_FALSE, projection)
        GL.glUniformMatrix4fv(uniform['view'], 1, GL.GL_FALSE, view)
        GL.glUniform1f(uniform['radius_scale'], radius / self.points.radius_reference)
        GL.glUniform1f(uniform['framebuffer_height'], height)
        GL.glBindVertexArray(self.point_vao)
        for begin, end in runs:
            offset = int(self.chunks['point_offset'][begin])
            count = int(self.chunks['point_offset'][end - 1] + self.chunks['point_count'][end - 1] - offset)
            GL.glDrawElements(GL.GL_POINTS, count, GL.GL_UNSIGNED_INT, ctypes.c_void_p(offset * 4))

    def draw_gaussians(self, shader, runs, projection, view, width, height):
        GL.glUseProgram(shader)
        uniform = self.uniforms[shader]
        GL.glUniformMatrix4fv(uniform['projection'], 1, GL.GL_FALSE, projection)
        GL.glUniformMatrix4fv(uniform['view'], 1, GL.GL_FALSE, view)
        GL.glUniform2f(uniform['framebuffer_size'], width, height)
        GL.glBindVertexArray(self.gaussian_vao)
        for begin, end in runs:
            offset = int(self.chunks['gaussian_offset'][begin])
            count = int(self.chunks['gaussian_offset'][end - 1] + self.chunks['gaussian_count'][end - 1] - offset)
            GL.glDrawArrays(GL.GL_POINTS, offset, count)

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
            self.resize(width, height)
            GL.glViewport(0, 0, width, height)
            for capability in (GL.GL_LIGHTING, GL.GL_FOG, GL.GL_CULL_FACE, GL.GL_ALPHA_TEST,
                               GL.GL_SCISSOR_TEST, GL.GL_STENCIL_TEST, GL.GL_SAMPLE_ALPHA_TO_COVERAGE,
                               GL.GL_BLEND, GL.GL_MULTISAMPLE):
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
            near, far = self.ranges(projection, view, height)
            self.draw_gaussians(self.gaussian_depth, far, projection, view, width, height)
            self.draw_points(self.point_depth, near, projection, view, radius, height)

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
            depth_mode = np.zeros(1, dtype=np.int32)
            if self.clip_control:
                GL.glGetIntegerv(GL.GL_CLIP_DEPTH_MODE, depth_mode)
            zero_to_one = int(depth_mode[0]) == GL.GL_ZERO_TO_ONE
            for shader in (self.gaussian_accum, self.point_accum):
                GL.glUseProgram(shader)
                GL.glUniform1i(self.uniforms[shader]['surface_depth'], 0)
                GL.glUniform1i(self.uniforms[shader]['zero_to_one'], zero_to_one)
            self.draw_gaussians(self.gaussian_accum, far, projection, view, width, height)
            self.draw_points(self.point_accum, near, projection, view, radius, height)

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
        GL.glDeleteVertexArrays(3, [self.empty_vao, self.point_vao, self.gaussian_vao])
        GL.glDeleteBuffers(2, [self.point_element, self.gaussian_vbo])
        for shader in (self.point_depth, self.point_accum, self.gaussian_depth,
                       self.gaussian_accum, self.resolve_program):
            GL.glDeleteProgram(shader)
