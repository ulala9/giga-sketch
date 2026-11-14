# Giga Sketch

A fork of Krita (GPL-3.0) aimed at digital painting and illustration. It keeps Krita's tools and adds three things: a Procreate-style brush feel, a Photoshop-style text tool, and a Photoshop-style UI layout.

## Base and layout
- Upstream: Krita, tag `v6.0.4.1` (Qt6). Working branch: `giga-main`.
- Remotes: `origin` = my fork (github.com/ulala9/giga-sketch), `upstream` = https://invent.kde.org/graphics/krita.git
- Folders: source `/home/ulala99/Documents/Apps/GigaSketch/giga-sketch`, build `.../GigaSketch/build`, install `.../GigaSketch/install`
- OS: Fedora KDE Plasma (Wayland default; test X11 too for tablet input).

## Build
```
cd /home/ulala99/Documents/Apps/GigaSketch/build
cmake -G Ninja ../giga-sketch -DBUILD_WITH_QT6=ON -DALLOW_UNSTABLE=QT6 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/home/ulala99/Documents/Apps/GigaSketch/install -DCMAKE_PREFIX_PATH=/home/ulala99/Documents/Apps/GigaSketch/deps/prefix -DKRITA_SIP_ABI_VERSION=13.8
ninja install
```
Run: `/home/ulala99/Documents/Apps/GigaSketch/install/bin/krita`
`deps/prefix` holds Immer, Zug and Lager (header-only, built from github.com/arximboldi; not packaged in Fedora). Sources are in `deps/src`.
Build and run after every change. Fix compile errors before moving on.

## Goals (in order)
1. **Isolate from stock Krita:** rename the config folder, app ID, and desktop file so Giga Sketch never shares settings with an installed Krita. Rebrand title, about screen, and icons with original artwork (do not reuse Krita's trademarked artwork).
2. **Procreate-style brush feel:** the stroke should feel like it is being "laid in", with a slightly vector-like, smoothed path.
   - Add a NEW smoothing mode alongside existing ones that fits a smooth curve through input points and stamps along it.
   - Add catch-up easing when the pen lifts (not a linear snap).
   - Experiment with how the in-progress stroke previews versus how it commits.
   - Expose controls: amount, pressure/speed influence.
3. **Photoshop-style text tool:** character panel (font, size, kerning, tracking, leading, baseline shift, case), paragraph panel (alignment, justification, indents, spacing), point text, box text, text on a path, on-canvas editing, non-destructive editable text layers, OpenType and variable fonts. Audit the current 6.0 text tool first. Upstream has been reworking text (branches like `rempt/T1004-recreate-the-text-tool`, `petrovic/text-tools`, `wolthera/text-layout-refactor`), so build on what exists. Use HarfBuzz, FreeType, and Qt text support instead of writing layout from scratch. Old text layers must still open correctly.
4. **Photoshop-style UI:** left toolbox, top options bar, dark theme, panel layout. Start with themes and workspaces, then Qt changes only where needed.

## Rules
- Add new modes and options. Do not replace or delete existing Krita code unless I say so. This keeps upstream merges manageable.
- Keep changes small and isolated. One feature per git branch (e.g. `feature/curve-smoothing`).
- Before coding, propose an approach and list the files you will touch. Wait for my approval.
- Stroke code is performance-sensitive and multithreaded. Flag any risk of lag, races, or crashes, and avoid blocking the paint thread.
- Imitate workflows only. Do not copy proprietary code, icons, names, or branding from Photoshop or Procreate.
- Preserve all GPL license headers and copyright notices.
- Explain code as you go. I am not an expert in C++/Qt.

## Testing
I test brush feel by hand with a pen tablet and will report what feels off. Compare against Procreate on: fast flicks, slow curves, tight corners, and lifting mid-curve.

## Notes
Keep a running summary of what you learn about the code in `NOTES.md` (stroke pipeline, text tool, UI structure) so it carries over between sessions.

## Roadmap (start here when resuming)
`giga-docs/ROADMAP.md` holds the phases, what's done, what's next, and decisions. It is imported below so it loads
every session. Update it (tick items, "Resume here", decisions) as part of every merge.

@/home/ulala99/Documents/Apps/GigaSketch/giga-sketch/giga-docs/ROADMAP.md
