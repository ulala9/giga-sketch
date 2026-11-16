# SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Giga Sketch: "Send to GigaSketch" Blender add-on (Blender Viewport Layer).

Renders what the 3D viewport shows (Solid / Material Preview / EEVEE), from the
active camera, into a PNG and tells a running Giga Sketch about it. Giga Sketch
then adds a "Blender: <camera>" layer above your active layer the first time
and only updates it after that. When you switch to Giga Sketch, it asks for a
fresh picture by itself (Preferences > General > File Handling can turn that off).

Install: Blender > Edit > Preferences > Add-ons > (top-right menu) Install from
Disk... > pick this file, then tick it. "Send to GigaSketch" is in the
viewport's right-click menu and View menu; its options are in the side bar (N)
under the "GigaSketch" tab. To give it a key: right-click the Send button in
the side bar > Assign Shortcut.

How it works, in short:
  1. GPUOffScreen + draw_view3d draws the viewport again, offscreen, at the
     size we want, with Blender's colour management on (so AgX/Filmic is
     applied and the picture looks like the viewport, not washed out).
  2. The pixels are read back from the GPU and written as an 8-bit PNG by a
     tiny writer below (no colour conversions we don't control), to
     "<file>.tmp" and then renamed over the real file (never half-written).
  3. A local socket ($XDG_RUNTIME_DIR/gigasketch-blender.sock) carries small
     JSON messages, one per line: we say "frame ready", Giga Sketch says its
     canvas size and "refresh please". A timer checks it 4 times a second, so
     Blender never waits for Giga Sketch.
"""

bl_info = {
    "name": "Send to GigaSketch",
    "author": "The Giga Sketch contributors",
    "version": (0, 2, 0),
    "blender": (4, 2, 0),
    "location": "3D Viewport > right-click menu, View menu, Sidebar (N) > GigaSketch",
    "description": "Send the viewport, seen from the camera, to a GigaSketch layer",
    "category": "3D View",
}

import json
import os
import re
import socket
import struct
import time
import zlib

import bpy
import gpu
import numpy as np

PROTOCOL_VERSION = 1
DEFAULT_FOLDER = os.path.join("~", "Pictures", "GigaSketch", "blender")
MAX_SIZE = 8192
POLL_SECONDS = 0.25
RECONNECT_SECONDS = 2.0


# ---------------------------------------------------------------- settings ---
class GigaSendSettings(bpy.types.PropertyGroup):
    """Stored on the scene, so each .blend remembers its own choices."""
    folder: bpy.props.StringProperty(
        name="Folder", subtype='DIR_PATH', default=DEFAULT_FOLDER,
        description="Where the pictures go (one PNG per .blend and camera). "
                    "'//' at the start means: next to this .blend")
    size_mode: bpy.props.EnumProperty(
        name="Size",
        items=[('CANVAS', "GigaSketch Canvas", "Same size as the open GigaSketch document "
                                               "(render resolution when GigaSketch isn't running)"),
               ('SCENE', "Render Resolution", "Use the scene's Output resolution (and %)"),
               ('CUSTOM', "Custom", "Use the width and height below")],
        default='CANVAS')
    width: bpy.props.IntProperty(name="Width", default=1920, min=16, max=MAX_SIZE)
    height: bpy.props.IntProperty(name="Height", default=1080, min=16, max=MAX_SIZE)
    transparent: bpy.props.BoolProperty(
        name="Transparent Background", default=False,
        description="Leave the world background out, so you can paint under the model")
    hide_overlays: bpy.props.BoolProperty(
        name="Hide Overlays", default=True,
        description="Leave out the grid, outlines of selected objects, gizmos and other overlays")
    last_info: bpy.props.StringProperty(default="")


# --------------------------------------------------------------- the link ---
class Link:
    """The connection to Giga Sketch. Module-level state, main thread only."""
    sock = None
    buffer = b""
    canvas = None          # (width, height) of the active GigaSketch document, or None
    next_try = 0.0
    announced_folder = None
    status = ""            # last ack, shown in the panel
    changes = 0            # bumped on every depsgraph update (scene edits, camera moves)
    last_sent = {}         # path -> signature of the last picture written there


def socket_path():
    env = os.environ.get("GIGA_BLENDER_SOCKET")
    if env:
        return env
    runtime = os.environ.get("XDG_RUNTIME_DIR") or "/tmp"
    return os.path.join(runtime, "gigasketch-blender.sock")


def link_connected():
    return Link.sock is not None


def link_close():
    if Link.sock is not None:
        try:
            Link.sock.close()
        except OSError:
            pass
    Link.sock = None
    Link.buffer = b""
    Link.canvas = None
    Link.announced_folder = None


def link_send(msg):
    if Link.sock is None:
        return False
    msg = dict(msg, version=PROTOCOL_VERSION)
    data = (json.dumps(msg) + "\n").encode("utf-8")
    try:
        Link.sock.setblocking(True)
        Link.sock.settimeout(0.5)  # local socket: only stalls if GigaSketch is frozen
        Link.sock.sendall(data)
        Link.sock.setblocking(False)
        return True
    except OSError:
        link_close()
        return False


def link_try_connect():
    if Link.sock is not None or time.monotonic() < Link.next_try:
        return
    Link.next_try = time.monotonic() + RECONNECT_SECONDS
    path = socket_path()
    if not os.path.exists(path):
        return
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        s.settimeout(0.2)
        s.connect(path)
        s.setblocking(False)
    except OSError:
        s.close()
        return
    Link.sock = s
    announce_folder(bpy.context.scene, force=True)


def announce_folder(scene, force=False):
    """Tell GigaSketch where our pictures live (so it knows which layers are ours)."""
    if scene is None or Link.sock is None:
        return
    folder = output_folder(scene.giga_send)
    if force or folder != Link.announced_folder:
        Link.announced_folder = folder
        link_send({"type": "hello", "app": "blender", "blender": bpy.app.version_string,
                   "folder": folder})


def link_poll():
    """Read whatever GigaSketch sent; returns the messages."""
    if Link.sock is None:
        return []
    try:
        while True:
            chunk = Link.sock.recv(65536)
            if not chunk:  # GigaSketch closed
                link_close()
                return []
            Link.buffer += chunk
            if len(Link.buffer) > 1 << 20:
                Link.buffer = b""
    except (BlockingIOError, InterruptedError):
        pass
    except OSError:
        link_close()
        return []
    messages = []
    while b"\n" in Link.buffer:
        line, Link.buffer = Link.buffer.split(b"\n", 1)
        try:
            msg = json.loads(line)
            if isinstance(msg, dict):
                messages.append(msg)
        except ValueError:
            pass
    return messages


def canvas_from(msg):
    w, h = msg.get("width"), msg.get("height")
    if isinstance(w, int) and isinstance(h, int) and w > 0 and h > 0:
        return (w, h)
    return None


# ----------------------------------------------------------------- helpers ---
def output_folder(settings):
    return os.path.abspath(bpy.path.abspath(os.path.expanduser(settings.folder or DEFAULT_FOLDER)))


def safe_name(text):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", text).strip("_") or "untitled"


def output_path(scene, camera):
    blend = os.path.splitext(os.path.basename(bpy.data.filepath))[0] or "untitled"
    view = camera.name if camera is not None else "View"
    return os.path.join(output_folder(scene.giga_send), "%s-%s.png" % (safe_name(blend), safe_name(view)))


def fit(width, height):
    """Keep the shape, at most MAX_SIZE on the long side (GigaSketch scales it up)."""
    scale = min(1.0, MAX_SIZE / max(width, height))
    return max(16, int(width * scale)), max(16, int(height * scale))


def output_size(scene, settings):
    if settings.size_mode == 'CANVAS' and Link.canvas:
        return fit(*Link.canvas)
    if settings.size_mode == 'CUSTOM':
        return settings.width, settings.height
    r = scene.render
    pct = r.resolution_percentage / 100.0
    return fit(max(16, int(r.resolution_x * pct)), max(16, int(r.resolution_y * pct)))


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


def biggest_viewport():
    """(window, area, region) of the largest 3D viewport, for refreshes from the timer."""
    best = None
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type != 'VIEW_3D':
                continue
            region = find_window_region(area)
            if region is None:
                continue
            if best is None or area.width * area.height > best[1].width * best[1].height:
                best = (window, area, region)
    return best


def matrices(scene, depsgraph, space, region, settings):
    camera = scene.camera
    if camera is not None:
        width, height = output_size(scene, settings)
        view_matrix = camera.matrix_world.inverted()
        projection_matrix = camera.calc_matrix_camera(
            depsgraph, x=width, y=height,
            scale_x=scene.render.pixel_aspect_x, scale_y=scene.render.pixel_aspect_y)
    else:
        # No camera: the current view, keeping the viewport's shape.
        width, _ = output_size(scene, settings)
        height = max(16, round(width * region.height / max(1, region.width)))
        rv3d = space.region_3d
        view_matrix = rv3d.view_matrix.copy()
        projection_matrix = rv3d.window_matrix.copy()
    return camera, width, height, view_matrix, projection_matrix


def capture(context, space, region, reason):
    """Render, write the PNG and tell GigaSketch. Returns (ok, message)."""
    t0 = time.perf_counter()
    scene = context.scene
    settings = scene.giga_send
    if space.shading.type == 'RENDERED' and scene.render.engine == 'CYCLES':
        return False, "Cycles isn't supported: use Solid or Material Preview (or EEVEE)"

    depsgraph = context.evaluated_depsgraph_get()
    camera, width, height, view_matrix, projection_matrix = matrices(scene, depsgraph, space, region, settings)
    path = output_path(scene, camera)

    # A refresh with nothing changed since the last picture: skip the work.
    def signature():
        return (Link.changes, width, height, tuple(map(tuple, view_matrix)), tuple(map(tuple, projection_matrix)),
                space.shading.type, settings.transparent, settings.hide_overlays)
    if reason == "refresh" and Link.last_sent.get(path) == signature() and os.path.exists(path):
        return True, "unchanged"

    overlays_were = space.overlay.show_overlays
    if settings.hide_overlays:
        space.overlay.show_overlays = False
    try:
        offscreen = gpu.types.GPUOffScreen(width, height, format='RGBA8')
    except Exception as e:  # e.g. size too big for the GPU
        space.overlay.show_overlays = overlays_were
        return False, "Could not make a %dx%d offscreen picture: %s" % (width, height, e)
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

    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        write_png(path, rgba, width, height)
    except OSError as e:
        return False, "Could not write %s: %s" % (path, e)
    # Taken after drawing: drawing may itself count as a depsgraph update.
    Link.last_sent[path] = signature()

    view_name = camera.name if camera is not None else "View"
    told = False
    if link_connected():
        announce_folder(scene)
        told = link_send({"type": "frame", "path": path, "width": width, "height": height,
                          "name": "Blender: " + view_name, "reason": reason})
    ms = (time.perf_counter() - t0) * 1000.0
    settings.last_info = "%dx%d, %s, %.0f ms" % (width, height, view_name, ms)
    if told:
        return True, "Sent %s to GigaSketch (%dx%d, %.0f ms)" % (view_name, width, height, ms)
    return True, "GigaSketch isn't running: saved %s" % path


def redraw_panels():
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == 'VIEW_3D':
                area.tag_redraw()


# ------------------------------------------------------------------- timer ---
def on_timer():
    try:
        was = link_connected()
        link_try_connect()
        refresh = False
        for msg in link_poll():
            kind = msg.get("type")
            if kind in ("canvas", "refresh"):
                Link.canvas = canvas_from(msg)
            if kind == "refresh":
                refresh = True
            elif kind == "ack":
                result = msg.get("result")
                layer = msg.get("layer", "")
                if result == "created":
                    Link.status = "GigaSketch added layer “%s”" % layer
                elif result == "updated":
                    Link.status = "GigaSketch updated “%s”" % layer
                elif msg.get("error"):
                    Link.status = "GigaSketch: " + msg["error"]
        if refresh:
            target = biggest_viewport()
            if target is not None:
                window, area, region = target
                with bpy.context.temp_override(window=window, area=area, region=region,
                                               space_data=area.spaces.active):
                    capture(bpy.context, area.spaces.active, region, "refresh")
        if was != link_connected() or refresh or Link.status:
            redraw_panels()
    except Exception as e:  # never let the timer die
        print("Send to GigaSketch:", e)
    return POLL_SECONDS


def on_depsgraph_update(scene, depsgraph):
    Link.changes += 1


# ---------------------------------------------------------------- operators ---
class VIEW3D_OT_giga_send(bpy.types.Operator):
    """Send the viewport, seen from the active camera, to GigaSketch"""
    bl_idname = "view3d.giga_send"
    bl_label = "Send to GigaSketch"

    @classmethod
    def poll(cls, context):
        return context.area is not None and context.area.type == 'VIEW_3D'

    def execute(self, context):
        region = find_window_region(context.area)
        if region is None:
            self.report({'ERROR'}, "No 3D viewport region found")
            return {'CANCELLED'}
        link_try_connect_now()
        ok, message = capture(context, context.space_data, region, "send")
        self.report({'INFO'} if ok else {'ERROR'}, message)
        return {'FINISHED'} if ok else {'CANCELLED'}


def link_try_connect_now():
    if not link_connected():
        Link.next_try = 0.0
        link_try_connect()


class VIEW3D_OT_giga_open_folder(bpy.types.Operator):
    """Open the folder with the pictures"""
    bl_idname = "view3d.giga_open_folder"
    bl_label = "Open Folder"

    def execute(self, context):
        folder = output_folder(context.scene.giga_send)
        os.makedirs(folder, exist_ok=True)
        bpy.ops.wm.path_open(filepath=folder)
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

        box = layout.box()
        if link_connected():
            if Link.canvas:
                box.label(text="Connected, canvas %d×%d" % Link.canvas, icon='LINKED')
            else:
                box.label(text="Connected, no document open", icon='LINKED')
        else:
            box.label(text="GigaSketch isn't running", icon='UNLINKED')
        if Link.status:
            box.label(text=Link.status)
        if s.last_info:
            box.label(text="Last: " + s.last_info)
        if context.scene.camera is None:
            layout.label(text="No camera: sends the current view", icon='INFO')

        col = layout.column()
        col.prop(s, "size_mode")
        if s.size_mode == 'CUSTOM':
            sub = col.column(align=True)
            sub.prop(s, "width")
            sub.prop(s, "height")
        col.prop(s, "transparent")
        col.prop(s, "hide_overlays")

        col = layout.column(align=True)
        col.label(text="Pictures folder:")
        col.prop(s, "folder", text="")
        col.operator(VIEW3D_OT_giga_open_folder.bl_idname, icon='FILE_FOLDER')


CLASSES = (GigaSendSettings, VIEW3D_OT_giga_send, VIEW3D_OT_giga_open_folder, VIEW3D_PT_giga_send)

# Menus that get a "Send to GigaSketch" entry: the right-click menus of the
# viewport (object mode and the edit modes) and the viewport's View menu.
MENUS = ("VIEW3D_MT_object_context_menu", "VIEW3D_MT_edit_mesh_context_menu",
         "VIEW3D_MT_pose_context_menu", "VIEW3D_MT_view")


def draw_menu_entry(self, context):
    self.layout.separator()
    self.layout.operator(VIEW3D_OT_giga_send.bl_idname, icon='EXPORT')


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Scene.giga_send = bpy.props.PointerProperty(type=GigaSendSettings)
    for name in MENUS:
        menu = getattr(bpy.types, name, None)
        if menu is not None:
            menu.append(draw_menu_entry)
    if on_depsgraph_update not in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.append(on_depsgraph_update)
    if not bpy.app.background and not bpy.app.timers.is_registered(on_timer):
        bpy.app.timers.register(on_timer, first_interval=1.0, persistent=True)


def unregister():
    if bpy.app.timers.is_registered(on_timer):
        bpy.app.timers.unregister(on_timer)
    if on_depsgraph_update in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(on_depsgraph_update)
    link_close()
    for name in MENUS:
        menu = getattr(bpy.types, name, None)
        if menu is not None:
            menu.remove(draw_menu_entry)
    del bpy.types.Scene.giga_send
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":  # running it from Blender's Text Editor also works
    register()
