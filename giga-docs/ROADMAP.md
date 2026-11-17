# Giga Sketch — roadmap and status

Loaded at the start of every session (imported from CLAUDE.md). **Keep it current:** tick items, add
branches, and move things between sections as part of every merge. How the code works lives in
`NOTES.md`; this file is only *what's done, what's next, and what's been decided*.

Last updated: 2026-11-16 (Annotations + Blender layer transform merged, G'MIC tried — all hand-tested)

## Resume here
- **Next up:** ask the user what to do next. Candidates: a Phase 5 blueprint (Bubble Guess, Blob Brush,
  Harmony Wheel), Phase 6 extras, or Blender bridge extras.
- Repo state: everything merged and pushed (2026-11-16: annotations, blender-layer-transform, plus earlier
  transform-last-stroke, stroke-points(-proportional), refboard-image-search, refboard-delete-key; all hand-tested).
  Text tools are "okish"; don't push more text work.
- Workflow that works: propose approach + files → wait for OK → branch → build/run → self-test where possible →
  user tests by hand → merge `--no-ff` into `giga-main` → push → update this file and NOTES.md.
- **The installed app is built from `giga-test`** (local only, never pushed or merged): `giga-main` + every branch
  still waiting for the user's hand test. Feature branches still start from `giga-main`. After committing on a
  feature branch: `git switch giga-test && git merge --no-ff <branch>` → `ninja install`, so the user always tests
  everything at once. After a feature merges into `giga-main`: merge `giga-main` into `giga-test`. If `giga-test`
  gets messy, recreate it: `git branch -D giga-test`, start from `giga-main`, re-merge the pending branches.
  Pending in `giga-test` now: `feature/giga-tool-icons` (batch 2, unreviewed, extreme low priority).
  To avoid full rebuilds, merges into `giga-test` can be recorded without touching files (see NOTES.md "giga-test without rebuilds").
- Edit ROADMAP/NOTES on `giga-main` in a side worktree (`git worktree add <scratch> giga-main`) to avoid branch
  switches in the build checkout (they touch file times → long rebuilds).
- UI checks: `giga-tools/selftest.sh all` (headless-ish UI self-tests, see NOTES.md "UI self-tests").
  Never screenshot the desktop; ask the user to look.

## Phase 1 — Isolate from stock Krita, rebrand
- [x] Own config/data/cache folders, app ID, desktop file (`feature/isolate-identity`)
- [x] Title, about screen, original icons/artwork (`feature/branding`)
- [x] Autosaves of untitled documents named `gigasketch-*`, startup recovery ignores Krita's (`fix/autosave-name`)
- [ ] (Windows only, ignore for now) `kritacrash.log`

## Phase 2 — Procreate-style brush feel
- [x] "Smooth Curve" smoothing mode: 1€ filter + centripetal Catmull-Rom, corner keeping (`feature/curve-smoothing`)
- [x] Catch-up easing on pen lift (animated tail), Catch-Up Time, Show Lag Line
- [x] Controls: amount, speed influence, pressure influence
- [x] Ctrl+Z during the catch-up tail (`fix/undo-during-catch-up`)
- [x] QuickShape (hold at stroke end → line/ellipse/…) and editing (`feature/quickshape`, `feature/quickshape-edit`)
- [x] Separate eraser brush (`feature/separate-eraser`)
- [x] **Hand-tested by the user vs Procreate — done.** Don't propose more tuning unless they report an issue.

## Phase 3 — Photoshop-style text tool
- [x] Audit of Krita 6.0 text tool (table in NOTES.md)
- [x] One layer per text, auto-named from its first line (`feature/text-ps-workflow`)
- [x] Paragraph space before/after, left/right indents (`feature/text-paragraph-spacing`)
- [x] Text options bar on top + easy moving (`feature/text-options-bar`, `feature/text-easy-move`)
- [x] Photoshop-style box text with resize handles, point ↔ paragraph conversion (`feature/text-boxes`)
- [x] Font picker: search, favourites, recent, previews, live preview (`feature/font-picker`)
- [x] Character and Paragraph panels (`feature/character-panel`)
- [ ] Horizontal / vertical text scale — **low priority** (user decision); don't suggest it. Approach already
  scoped (2026-11-13): non-uniform FT_Set_Char_Size per face (scale in KoCSSFontInfo + font cache key),
  `-giga-horizontal-scale`/`-giga-vertical-scale` style props, Character panel spin boxes.
- [ ] Hyphenation — **super low priority** (user decision); don't suggest it
- Already in Krita, no work needed: OpenType features, variable fonts, kerning/tracking/leading, baseline shift,
  case, faux bold/italic, underline/strike, text on a path, on-canvas editing.

## Phase 4 — Photoshop-style UI
- [x] Giga Dark theme, Photoshop shortcut scheme + input profile defaults (`feature/ui-*`)
- [x] Photoshop-like layout, left toolbox with tool groups, top options bar, right panel icon strip
- [x] Layers panel: lock row, PS button order, New/Delete on the left (`feature/ui-layers-panel`, `feature/layers-buttons-left`)
- [x] Layers panel: Duplicate button between New and Delete (`feature/layers-duplicate-button`, merged 2026-11-14)
- [x] Colour panel, menus wording, New Document dialog (`feature/ui-color-panel`, `feature/ui-menus-wording`, …)
- [x] Space-saving mode, panel flyouts (`feature/ui-space-saving`)
- [x] Interface modes Studio / Photoshop / Procreate, F9 (`feature/interface-modes`, `feature/procreate-*`, `feature/studio-*`)
- [x] Panels float inside the window: tear-off by tab/title/strip, drag back to dock, per-mode memory (`feature/in-window-panels`)
- [x] Floating panels stay on top on tab changes; header buttons instead of ≡ menu (`fix/floating-panel-tabs`)
- [x] Mode switch: no stale layout restore, each mode keeps its floating panels (`fix/mode-switch-double-restore`)
- [x] Smooth panel dragging (no self-snap), snapping only to adjacent panels, Ctrl = free move (`fix/panel-snap`)
- [x] Save Dated JPG, Ctrl+Space zoom toward pointer, no brush-switch popup
- [x] Lasso Fill tool (one-stroke loop fill, in the Fill group) + E erases with Lasso Fill / Enclose and Fill,
  also with custom blending (`feature/lasso-fill-erase`, merged 2026-11-15, hand-tested). Self-test `lassofill`.
- [x] Canvas look: dark surround, finer checkerboard, blue grid, Grid "Draw on" (`feature/procreate-canvas-look`, merged 2026-11-15)
- [x] User shortcut on Alt+<letter> beats a menu's Alt+letter (`fix/shortcut-beats-menu-letter`, merged 2026-11-15)
- [x] Taskbar icon: WM_CLASS `gigasketch` set before the app starts (`fix/taskbar-icon`)
- [x] No stray "No text" menu: Window menu is named `settings` in gigasketch.xmlgui so Toolbars Shown merges into it (`fix/no-text-menu`)
- Aesthetic polish (approved 2026-11-15, one branch each), merged 2026-11-15 after the user's look test:
  - [x] Brush cursor: two-tone outline, fade while painting, size readout (`feature/brush-cursor-look`). Self-test `cursorlook`.
    The fade only runs when Preferences > Cursor > While painting > "Show outline" is off (user has it on).
  - [x] Accent colour choice in Preferences > General (`feature/accent-color`). Self-test `accent`.
  - [x] Softer slider look, Preferences > General "Softer sliders" (`feature/slider-look`)
  - [x] Soft shadow around the canvas, Preferences > Display (`feature/canvas-shadow`)
  - [x] Interface motion: panel/flyout/tool-menu fades, mode-switch cross-fade, "Interface animations" (`feature/ui-motion`). Self-test `motion`.
  - [ ] Original line-art tool icons (`feature/giga-tool-icons`): **batch 1 (paint/fill) merged, style OK'd**; batch 2 (8 selection tools, move, transform, crop, sampler, measure) in giga-test, unreviewed; batches 3–4 **extreme low priority** (user, 2026-11-16): 3 (vector/text/shapes), 4 (hand/zoom + panel strip). Self-test `icons`.

## Phase 5 — Feature blueprints (doc "Giga Sketch Feature Blueprints")
- [x] Reference Board (`feature/reference-board`, merged 2026-11-14, hand-tested): Normalize, Arrange (5 styles,
  order, shape, spacing), Board panel with own camera, Send to Board / Canvas, click-to-focus, ←/→, Ctrl+click colour,
  drop/paste, look options, Resample to Current Size + shrink-on-save. Self-test `refboard`.
- [x] Board rotate/zoom/flip with the canvas (`feature/refboard-rotate-zoom`, `feature/refboard-flip`, merged 2026-11-15)
- [x] Board image search: small browser panel, Google Images / Bing / Pinterest (`feature/refboard-image-search`,
  merged 2026-11-16, hand-tested). Self-test `refsearch`.
- [x] G'MIC filters: Krita's G'MIC-Qt fork built as a plugin by `giga-tools/build-gmic.sh` (no source changes;
  2026-11-16). Hand-tested by the user 2026-11-16.
- [x] Board: Delete / Backspace / the Clear shortcut (Ctrl+Q for the user) delete the focused or outlined image
  (`fix/refboard-delete-key`, merged 2026-11-16).
- [x] **Annotations** (sticky notes, `feature/annotations`, merged 2026-11-16, hand-tested). A note is a
  special kind of reference image (GigaNote). Reference Images tool: Add Note (click to place), right-click menu,
  double-click to type, paste text; Edit > Add Note (no shortcut); View > Show Annotations; colours, 3 text sizes,
  collapse, Keep readable, 4 styles (Sticky, Card, Speech Bubble, Outline); Board + .kra/.krf. Self-test `notes`.
  2026-11-16: user reported a freeze on Edit > Add Note → fixed (no undoStack()->undo() from the editor; outside-click
  watcher starts after the opening click); re-tested OK.
- [ ] Bubble Guess, Blob Brush, Harmony Wheel: blueprinted only.
- [x] **Blender Viewport Layer** (`feature/blender-bridge`, merged 2026-11-16, hand-tested). Self-test `blender`;
  add-on `giga-tools/blender/giga_viewport_send.py`.
  A basic capture of Blender's viewport onto a GigaSketch layer, instead of importing models.
  - Scope (user): **on demand** (a "Send" in Blender / "Refresh" in GigaSketch, no live streaming);
    **Solid/EEVEE viewport look** (no Cycles); **one plain image** (no passes).
  - Blender side: small add-on (Python) renders the active camera view offscreen
    (`gpu.types.GPUOffScreen` + `draw_view3d`), reads the pixels back, sends them with the colours as Blender
    displays them (view transform AgX/Filmic applied, else it looks washed out).
  - Transport: shared-memory file in `/dev/shm` for pixels + local socket for small messages
    ("frame ready, W×H", "canvas is W×H", "refresh"); version number in the first message.
    Linux has no Spout/Syphon equivalent, so CPU copy.
  - GigaSketch side: new layer type modelled on the File Layer (`libs/ui/kis_file_layer.*`): shows the last
    capture, updates go through the normal async update path (never block the paint thread), no undo entries
    for refreshes, last capture saved in the .kra (opens fine without Blender), "Freeze to Paint Layer".
    Canvas size → Blender render size.
  - Step 0 (try before any C++): Blender script saves a PNG on "Send"; add it in GigaSketch as a File Layer
    (already auto-reloads, `kis_safe_document_loader.cpp`, 500 ms debounce). Tells whether the workflow fits.
  - Not in scope unless asked: live updates, Cycles, render passes (line art, colour IDs, AO, depth, normals),
    camera control from GigaSketch.
  - Branch: `feature/blender-bridge` (merged). **Step 0 done 2026-11-16**; user wanted less setup, so **step 1 done
    2026-11-16**: local socket bridge (`GigaBlenderBridge`), Send adds the layer above the active one (or a new
    document), later sends update it, switching to Giga Sketch asks Blender for a fresh picture (skipped if nothing
    changed; pref in General > File Handling), Blender matches the canvas size. Still a File Layer pointing at the
    PNG. Self-tests: `blender` + Blender-side scripts (NOTES).
    **Step 2 done 2026-11-16**: Blender layers = flagged File Layers whose picture is saved in the .kra
    (`giga-blender="1"`), no missing-file dialog/error, Layer > Refresh Blender Layers; Freeze = existing Convert to
    Paint Layer. Krita's kra loader/saver tests pass.
    **Step 3 done 2026-11-16** (speed): raw pixels via shared memory, decoded off the GUI thread, Blender layers no
    longer watch their PNG (no double load), canvas progress bar, Blender "Resolution: Full / Half". Hand-tested,
    merged 2026-11-16.
  - [x] **Scale/move a Blender layer** (user request 2026-11-16, `feature/blender-layer-transform`, merged 2026-11-16,
    hand-tested): Ctrl+T on a Blender layer works through a transform mask under it (added on first use, reused
    after), so every refresh keeps the transform. Self-test `blendertransform`.

## Phase 6 — Edit what you just drew (user request 2026-11-15)
- [x] Transform Last Stroke (`feature/transform-last-stroke`, merged 2026-11-16, hand-tested). Self-test `laststroke`.
- [x] Points mode: bend the stroke by dots, brush repaints it (`feature/stroke-points`, merged 2026-11-16).
- [x] Proportional editing in Points mode + smooth dragging (`feature/stroke-points-proportional`, merged 2026-11-16).
  Self-test `strokepoints`.
- Later, if wanted: Alt+drag a dot = thicker/thinner there (per-dot pressure, like Grease Pencil thickness);
  a top-bar button right after a stroke; editing strokes other than the last one (would need a vector stroke layer —
  much bigger, out of scope so far).

## Known issues / watch list
- If dragging a floating panel ever looks choppy again: run with `GIGA_PANEL_TRACE=1` and read the summary lines.
- After a Fedora update, link errors like `undefined reference to OpenColorIO_v2_4::…` / `Imf_3_2::…` = stale
  objects built against the old system library. Delete the `.o` files that mention the old namespace and rebuild
  (seen 2026-11-14: OCIO 2.4→2.5, OpenEXR 3.2→3.4).
- `selftest.sh all`: 4 Reference Board checks (board-shift-space-rotate, board-focus-keeps-rotation,
  board-reset-keeps-canvas-angle, arrange-canvas) fail in the full run but pass alone (seen 2026-11-16).
- `panelsnap` self-test segfaults in a small test window (test dereferences a panel frame its drag docked).
- After a crash/power loss: a 0-byte `build/libs/ui/kritaui_autogen/include/moc_*.cpp` causes link errors
  (`undefined reference to vtable/staticMetaObject`). Delete it and `touch` its header, then rebuild (2026-11-15).
- Pre-existing upstream test failures (not ours): TestSvgParser/Cloned/RoundTrip, SvgTextCursorTest RTL/vertical.

## Backlog / ideas (not scheduled)
- (empty — add user ideas here with the date)

## Decisions log
- 2026-11-16: Blender layer scaling = a transform mask added automatically by Ctrl+T (not a transform stored on the
  layer): reuses Krita's mask (undo, .kra, sharp redraw of each refresh); the mask shows in the Layers panel.
- 2026-11-16: Annotations: a note is a subclass of KisReferenceImage (not a separate object), saved as `<giga-note>`;
  created from the Reference Images tool + Edit > Add Note (right-click from any tool would clash with the popup
  palette). Editor finishes on a click outside, not on focus loss. An empty new note disappears on finish.
- 2026-11-16: Blender bridge transport = raw RGBA in /dev/shm + PNG written afterwards in the background
  (the PNG stays the File Layer's file); progress shown as a small bar on the canvas.
- 2026-11-16: Blender bridge: Send creates the layer itself (above the active layer), refresh when switching
  to Giga Sketch (on by default) instead of live streaming; one layer per camera.
- 2026-11-16: new tool icons (batches 3–4) → extreme low priority; don't suggest them.
- 2026-11-16: Board image search = small browser panel split inside the Board panel (not a separate panel, not
  APIs/scraping, not "use Firefox and drag"). Browser made on first Search click; WebEngine linked directly
  (~0.08 s startup), split into a lazy module only if needed.
- 2026-11-16: 3D = bridge to Blender (capture its viewport onto a layer), not model import. On demand,
  Solid/EEVEE, one plain image.
- 2026-11-15: Points drag = bend the last repaint on screen while dragging, real repaint on release (user chose
  option A over "repaint only the moving part"). Measured: user's stroke 3423 calls, ~100 ms per repaint.
- 2026-11-15: Proportional editing falloff acts on the whole line inside the radius (not only on the dots), radius
  measured along the line (Blender "connected only"); no O key (O = increase opacity).
- 2026-11-15: Transform Last Stroke "Points" mode = repaint the recorded dabs along a bent path (not pixel warp);
  last stroke only; refuses the same brushes as Pixels mode. Toggle = same shortcut again + top-bar buttons.
- 2026-11-15: Transform Last Stroke = pixel transform of the isolated stroke (reuses Krita's Transform tool, user's
  lasso+Ctrl+T habit) rather than path-redraw first. Refuses smudge/deform/filter/clone brushes, erasers and
  non-Normal blending instead of "taking the pixels as-is". Shortcut Ctrl+Shift+Alt+T (Ctrl+Alt+T = Konsole in KDE).
- 2026-11-15: Lasso Fill is its own tool (own settings, lasso only, contour regions on); erase mode has no
  special cursor yet (only the top-bar eraser button shows it) — add a cue only if the user asks.
- 2026-11-13: brush feel tuning considered done (user tested).
- 2026-11-13: hyphenation → super low priority.
- 2026-11-13: text scale → low priority; text tools are "okish" for now.
- 2026-11-13: panel headers show Float/Dock and Close as buttons, not a dropdown (fewer clicks).
- 2026-11-14: Reference Board settings (layout, look, shrink-on-save) are global preferences; the arrangement itself
  is per document (saved in the .kra).
- 2026-11-13: autosaves of saved files keep Krita's `<file>.kra-autosave.kra` name (only untitled ones renamed).
