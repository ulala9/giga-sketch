# SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Giga Sketch: "Send to GigaSketch" Blender add-on (Blender Viewport Layer, step 0).

Renders what the 3D viewport shows (Solid / Material Preview / EEVEE), from the
active camera, into a PNG. In GigaSketch, add that PNG as a File Layer
(Layer > New > File Layer): it reloads by itself every time you press Send.

Install: Blender > Edit > Preferences > Add-ons > (top-right menu) Install from
Disk... > pick this file, then tick it. The panel is in the 3D viewport's
side bar (N) under the "GigaSketch" tab. Right-click the Send button >
Assign Shortcut to give it a key.

How it works, in short:
  1. GPUOffScreen + draw_view3d draws the viewport again, offscreen, at the
     size we want, with Blender's colour management on (so AgX/Filmic is
     applied and the picture looks like the viewport, not washed out).
  2. The pixels are read back from the GPU and written as an 8-bit PNG by a
     tiny writer below (no colour conversions we don't control).
  3. The PNG is written to "<file>.tmp" and then renamed over the real file,
     so GigaSketch never reads a half-written picture.
"""

bl_info = {
    "name": "Send to GigaSketch",
    "author": "The Giga Sketch contributors",
    "version": (0, 1, 0),
    "blender": (4, 2, 0),
    "location": "3D Viewport > Sidebar (N) > GigaSketch",
    "description": "Render the viewport from the camera into a PNG that a GigaSketch File Layer shows",
    "category": "3D View",
}

import os
import struct
import time
import zlib

import bpy
import gpu
import numpy as np

DEFAULT_PATH = os.path.join("~", "Pictures", "GigaSketch", "blender-view.png")


# ---------------------------------------------------------------- settings ---
class GigaSendSettings(bpy.types.PropertyGroup):
    """Stored on the scene, so each .blend remembers its own output file."""
    filepath: bpy.props.StringProperty(
        name="File", subtype='FILE_PATH', default=DEFAULT_PATH,
        description="PNG to write. Add the same file in GigaSketch as a File Layer. "
                    "'//' at the start means: next to this .blend")
    size_mode: bpy.props.EnumProperty(
        name="Size",
        items=[('SCENE', "Render Resolution", "Use the scene's Output resolution (and %)"),
               ('CUSTOM', "Custom", "Use the width and height below")],
        default='SCENE')
    width: bpy.props.IntProperty(name="Width", default=1920, min=16, max=8192)
    height: bpy.props.IntProperty(name="Height", default=1080, min=16, max=8192)
    transparent: bpy.props.BoolProperty(
        name="Transparent Background", default=False,
        description="Leave the world background out, so you can paint under the model")
    hide_overlays: bpy.props.BoolProperty(
        name="Hide Overlays", default=True,
        description="Leave out the grid, outlines of selected objects, gizmos and other overlays")
    last_info: bpy.props.StringProperty(default="")


# ----------------------------------------------------------------- helpers ---
def output_size(scene, settings):
    if settings.size_mode == 'CUSTOM':
        return settings.width, settings.height
    r = scene.render
    pct = r.resolution_percentage / 100.0
    return max(16, int(r.resolution_x * pct)), max(16, int(r.resolution_y * pct))


def write_png(path, rgba, width, height):
    """Write an 8-bit RGBA PNG. rgba: uint8 array (height, width, 4), top row first."""
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))
    # Each PNG row starts with a filter byte (0 = none).
    rows = np.zeros((height, width * 4 + 1), dtype=np.uint8)
    rows[:, 1:] = rgba.reshape(height, width * 4)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
           + chunk(b"sRGB", b"\x00")  # the pixels are display (sRGB) colours
           + chunk(b"IDAT", zlib.compress(rows.tobytes(), 3))
           + chunk(b"IEND", b""))
    tmp = path + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(png)
    os.replace(tmp, path)  # atomic: readers see the old file or the new one


def buffer_to_array(buf, count):
    """gpu.types.Buffer -> flat numpy uint8 array (fast path, then fallback)."""
    try:
        arr = np.frombuffer(buf, dtype=np.uint8, count=count)
        if arr.size == count:
            return arr.copy()
    except (TypeError, ValueError):
        pass
    buf.dimensions = count
    return np.array(buf.to_list(), dtype=np.uint8)


def find_window_region(area):
    for region in area.regions:
        if region.type == 'WINDOW':
            return region
    return None


# ---------------------------------------------------------------- operator ---
class VIEW3D_OT_giga_send(bpy.types.Operator):
    """Render the viewport from the active camera into the GigaSketch PNG"""
    bl_idname = "view3d.giga_send"
    bl_label = "Send to GigaSketch"

    @classmethod
    def poll(cls, context):
        return context.area is not None and context.area.type == 'VIEW_3D'

    def execute(self, context):
        t0 = time.perf_counter()
        scene = context.scene
        settings = scene.giga_send
        space = context.space_data
        region = find_window_region(context.area)
        if region is None:
            self.report({'ERROR'}, "No 3D viewport region found")
            return {'CANCELLED'}
        rv3d = space.region_3d

        if space.shading.type == 'RENDERED' and scene.render.engine == 'CYCLES':
            self.report({'WARNING'}, "Cycles isn't supported: use Solid or Material Preview (or EEVEE)")
            return {'CANCELLED'}

        depsgraph = context.evaluated_depsgraph_get()
        camera = scene.camera
        if camera is not None:
            width, height = output_size(scene, settings)
            view_matrix = camera.matrix_world.inverted()
            projection_matrix = camera.calc_matrix_camera(
                depsgraph, x=width, y=height,
                scale_x=scene.render.pixel_aspect_x, scale_y=scene.render.pixel_aspect_y)
            source = "camera '%s'" % camera.name
        else:
            # No camera: send the current view, keeping the viewport's shape.
            width, _ = output_size(scene, settings)
            height = max(16, round(width * region.height / max(1, region.width)))
            view_matrix = rv3d.view_matrix.copy()
            projection_matrix = rv3d.window_matrix.copy()
            source = "current view (no camera in the scene)"

        overlays_were = space.overlay.show_overlays
        if settings.hide_overlays:
            space.overlay.show_overlays = False
        try:
            offscreen = gpu.types.GPUOffScreen(width, height, format='RGBA8')
        except Exception as e:  # e.g. size too big for the GPU
            space.overlay.show_overlays = overlays_were
            self.report({'ERROR'}, "Could not make a %dx%d offscreen picture: %s" % (width, height, e))
            return {'CANCELLED'}
        try:
            offscreen.draw_view3d(scene, context.view_layer, space, region,
                                  view_matrix, projection_matrix,
                                  do_color_management=True,
                                  draw_background=not settings.transparent)
            buf = offscreen.texture_color.read()
        finally:
            offscreen.free()
            space.overlay.show_overlays = overlays_were

        rgba = buffer_to_array(buf, width * height * 4).reshape(height, width, 4)
        rgba = np.flipud(rgba)  # GPU rows are bottom-up, PNG rows top-down
        if settings.transparent:
            # The GPU gives premultiplied colour; PNG wants it straight.
            f = rgba.astype(np.float32)
            a = f[..., 3:4]
            f[..., :3] = np.where(a > 0, np.minimum(f[..., :3] * 255.0 / np.maximum(a, 1.0), 255.0), 0.0)
            rgba = (f + 0.5).astype(np.uint8)
        else:
            rgba = np.ascontiguousarray(rgba)
            rgba[..., 3] = 255

        path = os.path.abspath(bpy.path.abspath(os.path.expanduser(settings.filepath)))
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            write_png(path, rgba, width, height)
        except OSError as e:
            self.report({'ERROR'}, "Could not write %s: %s" % (path, e))
            return {'CANCELLED'}

        ms = (time.perf_counter() - t0) * 1000.0
        settings.last_info = "%dx%d, %s, %.0f ms" % (width, height, source, ms)
        self.report({'INFO'}, "Sent %dx%d to %s (%.0f ms)" % (width, height, path, ms))
        return {'FINISHED'}


class VIEW3D_OT_giga_copy_path(bpy.types.Operator):
    """Copy the PNG's full path, to paste into GigaSketch's File Layer dialog"""
    bl_idname = "view3d.giga_copy_path"
    bl_label = "Copy Path"

    def execute(self, context):
        s = context.scene.giga_send
        context.window_manager.clipboard = os.path.abspath(bpy.path.abspath(os.path.expanduser(s.filepath)))
        self.report({'INFO'}, "Copied: " + context.window_manager.clipboard)
        return {'FINISHED'}


# ------------------------------------------------------------------- panel ---
class VIEW3D_PT_giga_send(bpy.types.Panel):
    bl_label = "Send to GigaSketch"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "GigaSketch"

    def draw(self, context):
        s = context.scene.giga_send
        layout = self.layout
        row = layout.row()
        row.scale_y = 1.6
        row.operator(VIEW3D_OT_giga_send.bl_idname, text="Send", icon='EXPORT')
        if s.last_info:
            layout.label(text="Last: " + s.last_info)
        if context.scene.camera is None:
            layout.label(text="No camera: sends the current view", icon='INFO')

        col = layout.column(align=True)
        col.prop(s, "filepath", text="")
        col.operator(VIEW3D_OT_giga_copy_path.bl_idname, icon='COPYDOWN')

        col = layout.column()
        col.prop(s, "size_mode")
        if s.size_mode == 'CUSTOM':
            sub = col.column(align=True)
            sub.prop(s, "width")
            sub.prop(s, "height")
        col.prop(s, "transparent")
        col.prop(s, "hide_overlays")


CLASSES = (GigaSendSettings, VIEW3D_OT_giga_send, VIEW3D_OT_giga_copy_path, VIEW3D_PT_giga_send)


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Scene.giga_send = bpy.props.PointerProperty(type=GigaSendSettings)


def unregister():
    del bpy.types.Scene.giga_send
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":  # running it from Blender's Text Editor also works
    register()
