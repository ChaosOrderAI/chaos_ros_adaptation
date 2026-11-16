#!/usr/bin/env python3
"""Check Gaussian surface occlusion, GL state, resize, and resource cleanup.

Uses a tiny synthetic cloud and one hidden window; no ROS or running scene is
modified. Run with the viewer Python environment, for example:

  __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia \\
    /home/yunze/mujoco_playground/.venv/bin/python scripts/check_gaussian_surface.py

Optional --output-dir saves frame PNGs and a JSON report. Software GL requires
--allow-software and is identified explicitly in the report.
"""
import argparse
import json
from pathlib import Path
import time

import glfw
import mujoco
import numpy as np
from OpenGL import GL

from pcd_gpu_viewer import PointRenderer, save_png


def gl_state():
    names = ('GL_DRAW_FRAMEBUFFER_BINDING', 'GL_READ_FRAMEBUFFER_BINDING',
             'GL_CURRENT_PROGRAM', 'GL_VERTEX_ARRAY_BINDING', 'GL_ACTIVE_TEXTURE',
             'GL_VIEWPORT', 'GL_DEPTH_FUNC', 'GL_DEPTH_WRITEMASK', 'GL_COLOR_WRITEMASK',
             'GL_DRAW_BUFFER', 'GL_READ_BUFFER', 'GL_BLEND_SRC_RGB', 'GL_BLEND_DST_RGB',
             'GL_BLEND_EQUATION_RGB', 'GL_PACK_ALIGNMENT')
    result = {name: tuple(np.atleast_1d(GL.glGetIntegerv(getattr(GL, name))).tolist())
              for name in names}
    for name in ('GL_DEPTH_TEST', 'GL_BLEND', 'GL_MULTISAMPLE', 'GL_SAMPLE_ALPHA_TO_COVERAGE',
                 'GL_FRAMEBUFFER_SRGB', 'GL_CULL_FACE'):
        result[name] = bool(GL.glIsEnabled(getattr(GL, name)))
    result['clear_color'] = tuple(GL.glGetFloatv(GL.GL_COLOR_CLEAR_VALUE))
    result['clear_depth'] = float(GL.glGetDoublev(GL.GL_DEPTH_CLEAR_VALUE))
    original = int(GL.glGetIntegerv(GL.GL_ACTIVE_TEXTURE))
    for unit in range(3):
        GL.glActiveTexture(GL.GL_TEXTURE0 + unit)
        result[f'texture{unit}'] = int(GL.glGetIntegerv(GL.GL_TEXTURE_BINDING_2D))
    GL.glActiveTexture(original)
    return result


def make_cloud(foreground):
    # A red surface and a farther green surface occupy the same screen area.
    xy = np.stack(np.meshgrid(np.linspace(-1.8, 1.8, 81),
                             np.linspace(-1.4, 1.4, 61)), axis=-1).reshape(-1, 2)
    dtype = np.dtype([('xyz', '<f4', (3,)), ('rgb', 'u1', (4,))])
    blocks = []
    for z, rgb in ((0., (255, 0, 0, 255)), (-.4, (0, 255, 0, 255))):
        values = np.zeros(len(xy), dtype=dtype)
        values['xyz'][:, :2] = xy
        values['xyz'][:, 2] = z
        values['rgb'] = rgb
        blocks.append(values)
    if foreground:
        xy_front = np.stack(np.meshgrid(np.linspace(-.18, .18, 13),
                                       np.linspace(-.18, .18, 13)), axis=-1).reshape(-1, 2)
        values = np.zeros(len(xy_front), dtype=dtype)
        values['xyz'][:, :2] = xy_front
        values['xyz'][:, 2] = .7
        values['rgb'] = (0, 255, 0, 255)
        blocks.append(values)
    values = np.concatenate(blocks)
    normals = np.zeros((len(values), 3), dtype=np.float32)
    normals[:, 2] = 1
    radii = np.full(len(values), .04, dtype=np.float32)
    return PointRenderer(values, normals, radii, .04)


def read_pixels(width, height):
    GL.glReadBuffer(GL.GL_BACK)
    GL.glPixelStorei(GL.GL_PACK_ALIGNMENT, 1)
    pixels = GL.glReadPixels(0, 0, width, height, GL.GL_RGB, GL.GL_UNSIGNED_BYTE)
    return np.frombuffer(pixels, dtype=np.uint8).reshape(height, width, 3)


def resize_window(window, width, height):
    glfw.set_window_size(window, width, height)
    deadline = time.monotonic() + 2
    while glfw.get_window_size(window) != (width, height):
        glfw.poll_events()
        if time.monotonic() >= deadline:
            raise AssertionError(f'window did not resize to {width}x{height}')
        time.sleep(.005)
    # Deliver the framebuffer-size event before reading the device-pixel size.
    glfw.poll_events()
    return glfw.get_framebuffer_size(window)


def check_pixels(native, pixels, foreground):
    height, width = pixels.shape[:2]
    center = pixels[height // 2 - 3:height // 2 + 4,
                    width // 2 - 3:width // 2 + 4].mean(axis=(0, 1))
    blue_error = 0
    if foreground:
        if not (center[1] > 200 and center[0] < 15 and center[2] < 15):
            raise AssertionError(f'foreground green cloud failed to cover blue box: {center}')
    else:
        if not (center[2] > 100 and center[0] < 15 and center[1] < 15):
            raise AssertionError(f'background cloud incorrectly covered blue box: {center}')
        if np.count_nonzero(pixels[:, :, 1] > 100):
            raise AssertionError('far green cloud leaked through the nearer red surface')
        # Including partially covered pixels verifies native robot MSAA edges.
        blue_error = int(np.max(np.abs(pixels[:, :, 2].astype(int) - native[:, :, 2].astype(int))))
        if blue_error > 2:
            raise AssertionError(f'native blue box/edge coverage changed by {blue_error}/255')
    region = pixels[height // 2 - 10:height // 2 + 10,
                    width // 4 - 10:width // 4 + 10].mean(axis=(0, 1))
    if not (region[0] > 180 and region[1] < 15 and region[2] < 15):
        raise AssertionError(f'red surface missing or mixed with its farther green layer: {region}')
    return {'center_rgb': center.tolist(), 'native_blue_max_error': blue_error}


def close_points(points):
    gaussian = points.gaussian
    resources = ((GL.glIsFramebuffer, (gaussian.depth_fbo, gaussian.accum_fbo)),
                 (GL.glIsTexture, (gaussian.depth_texture, gaussian.accum_texture)),
                 (GL.glIsVertexArray, (points.vao, gaussian.empty_vao)),
                 (GL.glIsProgram, (points.program, gaussian.depth_program,
                                   gaussian.accum_program, gaussian.resolve_program)))
    points.close()
    for exists, identifiers in resources:
        if any(exists(identifier) for identifier in identifiers):
            raise AssertionError('renderer resources remain allocated after close')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, help='Optional PNG and JSON report directory')
    parser.add_argument('--samples', choices=(0, 2, 4, 8), type=int, default=4)
    parser.add_argument('--allow-software', action='store_true', help='Allow explicit software-GL diagnostics')
    args = parser.parse_args()
    if args.output_dir:
        args.output_dir = args.output_dir.expanduser().resolve()
        args.output_dir.mkdir(parents=True, exist_ok=True)
    if not glfw.init():
        raise RuntimeError('GLFW cannot open DISPLAY; run in the desktop session')
    window = context = points = sentinels = None
    report = {'cases': []}
    try:
        glfw.window_hint(glfw.VISIBLE, glfw.FALSE)
        glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
        glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
        glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_COMPAT_PROFILE)
        glfw.window_hint(glfw.SAMPLES, args.samples)
        window = glfw.create_window(320, 240, 'Gaussian synthetic QA', None, None)
        if not window:
            raise RuntimeError('could not create the hidden OpenGL window')
        glfw.make_context_current(window)
        glfw.swap_interval(0)
        renderer = GL.glGetString(GL.GL_RENDERER).decode()
        software = any(name in renderer.lower() for name in ('llvmpipe', 'softpipe', 'swrast', 'software'))
        if software and not args.allow_software:
            raise RuntimeError('software renderer detected; select the GPU or explicitly use --allow-software')
        report.update(renderer=renderer, software=software, samples=int(GL.glGetIntegerv(GL.GL_SAMPLES)))
        print('Renderer:', renderer)
        model = mujoco.MjModel.from_xml_string('''<mujoco>
          <visual><headlight ambient=".7 .7 .7" diffuse=".3 .3 .3" specular="0 0 0"/>
            <map znear=".005" zfar="200"/></visual><statistic extent="3"/>
          <worldbody><geom type="box" pos="0 0 .25" size=".35 .35 .15" rgba="0 0 1 1"/></worldbody>
        </mujoco>''')
        data = mujoco.MjData(model)
        mujoco.mj_forward(model, data)
        scene = mujoco.MjvScene(model, maxgeom=100)
        scene.flags[mujoco.mjtRndFlag.mjRND_SHADOW] = False
        scene.flags[mujoco.mjtRndFlag.mjRND_REFLECTION] = False
        context = mujoco.MjrContext(model, mujoco.mjtFontScale.mjFONTSCALE_100)
        mujoco.mjr_setBuffer(mujoco.mjtFramebuffer.mjFB_WINDOW, context)
        camera = mujoco.MjvCamera()
        camera.distance, camera.elevation, camera.azimuth = 4, -90, 90
        camera.lookat[:] = (0, 0, 0)
        options = mujoco.MjvOption()
        sentinels = GL.glGenTextures(3)
        for unit, texture in enumerate(sentinels):
            GL.glActiveTexture(GL.GL_TEXTURE0 + unit)
            GL.glBindTexture(GL.GL_TEXTURE_2D, int(texture))
        GL.glActiveTexture(GL.GL_TEXTURE0)
        for foreground in (False, True):
            points = make_cloud(foreground)
            points.set_mode('gaussian')
            for index, dimensions in enumerate(((320, 240), (400, 280), (320, 240))):
                width, height = resize_window(window, *dimensions)
                mujoco.mjv_updateScene(model, data, options, None, camera,
                                      mujoco.mjtCatBit.mjCAT_ALL, scene)
                mujoco.mjr_render(mujoco.MjrRect(0, 0, width, height), scene, context)
                native = read_pixels(width, height)
                GL.glActiveTexture(GL.GL_TEXTURE2)
                before = gl_state()
                points.render(height, .04)
                after = gl_state()
                changed = {key: (before[key], after[key]) for key in before if before[key] != after[key]}
                if changed:
                    raise AssertionError(f'GL state leaked: {changed}')
                case = {'foreground': foreground, 'size': [width, height], 'state_restored': True}
                case.update(check_pixels(native, read_pixels(width, height), foreground))
                if GL.glGetError() != GL.GL_NO_ERROR:
                    raise AssertionError('OpenGL error after Gaussian render')
                report['cases'].append(case)
                if args.output_dir:
                    save_png(args.output_dir / f'foreground-{int(foreground)}-{index}.png', width, height)
                print(f'PASS foreground={foreground} size={width}x{height}; occlusion and GL state')
            close_points(points)
            points = None
        report['passed'] = True
        report['resources_released'] = True
        if args.output_dir:
            (args.output_dir / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print('All Gaussian depth, native MSAA edge, state, resize, and resource checks passed.')
    finally:
        if points:
            points.close()
        if sentinels is not None:
            GL.glDeleteTextures(sentinels)
        if context:
            context.free()
        if window:
            glfw.destroy_window(window)
        glfw.terminate()


if __name__ == '__main__':
    main()
