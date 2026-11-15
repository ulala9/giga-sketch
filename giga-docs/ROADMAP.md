# Giga Sketch — roadmap and status

Loaded at the start of every session (imported from CLAUDE.md). **Keep it current:** tick items, add
branches, and move things between sections as part of every merge. How the code works lives in
`NOTES.md`; this file is only *what's done, what's next, and what's been decided*.

Last updated: 2026-11-15 (Reference Board rotate/zoom built)

## Resume here
- **Next up:** the user's look test of the six aesthetic branches (Phase 4 list below), then icon batches 2–4.
- Also waiting: `feature/procreate-canvas-look` (unmerged, waiting for the user's hand test): dark canvas surround,
  finer checkerboard, light blue grid, Grid panel "Draw on" (Canvas / Canvas + background / Background only with
  screen-fixed Size), grid limits 10000 px, two upstream grid fixes. Merge after the user OKs it.
- Also waiting for a hand test (all 2026-11-15, all in `giga-test`, all with a Preferences on/off switch):
  - `fix/shortcut-beats-menu-letter` — a user shortcut on Alt+<letter> (e.g. Feather Selection on Alt+A) wins over a
    menu's Alt+letter; the menu drops its underline letter while the key is taken. Self-test `menuletter`.
  - `feature/accent-color` — Preferences > General > Accent colour (presets + Custom). Self-test `accent`.
  - `feature/slider-look` — softer slider spin boxes (Preferences > General > "Softer sliders").
  - `feature/canvas-shadow` — soft shadow around the image (Preferences > Display).
  - `feature/ui-motion` — fades for floating panels/flyouts/tool menus, cross-fade on mode switch
    (Preferences > General > "Interface animations"). Self-test `motion`.
  - `feature/brush-cursor-look` — two-tone brush outline, fade while painting, "24 px" readout on Shift+drag
    resize. Self-test `cursorlook`.
  - `feature/refboard-rotate-zoom` — Reference Board gets the canvas navigation: Ctrl+Space drag zoom, Shift+Space
    drag rotate (+Alt = stepped), Reset Canvas Rotation key (F1 for the user) turns it upright. Follows Canvas Input
    Settings / Keyboard Shortcuts. Self-test `refboard` (4 new checks).
  - `feature/refboard-flip` (built on `feature/refboard-rotate-zoom`; merging it merges both) — Board flips with the
    canvas (Mirror View, F2), Board ▸ Look ▸ "Flip With Canvas" (default on), and turns with the canvas rotation,
    "Rotate With Canvas" (default on; Shift+Space on the Board adds its own turn, F1 over the Board removes it).
    Self-test `refboard`.
  - `feature/giga-tool-icons` — original tool icons, batch 1 (paint, retouch, fill tools); needs a restart.
    Self-test `icons`. Next batches: the remaining toolbox tools.
- Repo state: everything else merged and pushed (`feature/lasso-fill-erase` on 2026-11-15).
  Text tools are "okish"; don't push more text work.
- Workflow that works: propose approach + files → wait for OK → branch → build/run → self-test where possible →
  user tests by hand → merge `--no-ff` into `giga-main` → push → update this file and NOTES.md.
- **The installed app is built from `giga-test`** (local only, never pushed or merged): `giga-main` + every branch
  still waiting for the user's hand test. Feature branches still start from `giga-main`. After committing on a
  feature branch: `git switch giga-test && git merge --no-ff <branch>` → `ninja install`, so the user always tests
  everything at once. After a feature merges into `giga-main`: merge `giga-main` into `giga-test`. If `giga-test`
  gets messy, recreate it: `git branch -D giga-test`, start from `giga-main`, re-merge the pending branches.
  Pending in `giga-test` now: `feature/procreate-canvas-look`, `fix/shortcut-beats-menu-letter`,
  `feature/brush-cursor-look`, `feature/accent-color`, `feature/slider-look`, `feature/canvas-shadow`,
  `feature/ui-motion`, `feature/giga-tool-icons`, `feature/refboard-rotate-zoom`, `feature/refboard-flip`.
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
- [x] Taskbar icon: WM_CLASS `gigasketch` set before the app starts (`fix/taskbar-icon`)
- [x] No stray "No text" menu: Window menu is named `settings` in gigasketch.xmlgui so Toolbars Shown merges into it (`fix/no-text-menu`)
- Aesthetic polish (approved 2026-11-15, one branch each). Built, in `giga-test`, **waiting for the user's look/hand test**:
  - [ ] Brush cursor: two-tone outline, fade while painting, size readout (`feature/brush-cursor-look`). Self-test `cursorlook`.
    The fade only runs when Preferences > Cursor > While painting > "Show outline" is off (user has it on).
  - [ ] Accent colour choice in Preferences > General (`feature/accent-color`). Self-test `accent`.
  - [ ] Softer slider look, Preferences > General "Softer sliders" (`feature/slider-look`)
  - [ ] Soft shadow around the canvas, Preferences > Display (`feature/canvas-shadow`)
  - [ ] Interface motion: panel/flyout/tool-menu fades, mode-switch cross-fade, "Interface animations" (`feature/ui-motion`). Self-test `motion`.
  - [ ] Original line-art tool icons (`feature/giga-tool-icons`): **batch 1 (paint/fill) done**, waiting for the user's
    style feedback before batch 2 (select/move/crop/sample), 3 (vector/text/shapes), 4 (hand/zoom + panel strip). Self-test `icons`.

## Phase 5 — Feature blueprints (doc "Giga Sketch Feature Blueprints")
- [x] Reference Board (`feature/reference-board`, merged 2026-11-14, hand-tested): Normalize, Arrange (5 styles,
  order, shape, spacing), Board panel with own camera, Send to Board / Canvas, click-to-focus, ←/→, Ctrl+click colour,
  drop/paste, look options, Resample to Current Size + shrink-on-save. Self-test `refboard`.
- [ ] Annotations, Bubble Guess, Blob Brush, Harmony Wheel, 3D Model Layer: blueprinted only.

## Known issues / watch list
- If dragging a floating panel ever looks choppy again: run with `GIGA_PANEL_TRACE=1` and read the summary lines.
- After a Fedora update, link errors like `undefined reference to OpenColorIO_v2_4::…` / `Imf_3_2::…` = stale
  objects built against the old system library. Delete the `.o` files that mention the old namespace and rebuild
  (seen 2026-11-14: OCIO 2.4→2.5, OpenEXR 3.2→3.4).
- `panelsnap` self-test segfaults in a small test window (test dereferences a panel frame its drag docked).
- After a crash/power loss: a 0-byte `build/libs/ui/kritaui_autogen/include/moc_*.cpp` causes link errors
  (`undefined reference to vtable/staticMetaObject`). Delete it and `touch` its header, then rebuild (2026-11-15).
- Pre-existing upstream test failures (not ours): TestSvgParser/Cloned/RoundTrip, SvgTextCursorTest RTL/vertical.

## Backlog / ideas (not scheduled)
- (empty — add user ideas here with the date)

## Decisions log
- 2026-11-15: Lasso Fill is its own tool (own settings, lasso only, contour regions on); erase mode has no
  special cursor yet (only the top-bar eraser button shows it) — add a cue only if the user asks.
- 2026-11-13: brush feel tuning considered done (user tested).
- 2026-11-13: hyphenation → super low priority.
- 2026-11-13: text scale → low priority; text tools are "okish" for now.
- 2026-11-13: panel headers show Float/Dock and Close as buttons, not a dropdown (fewer clicks).
- 2026-11-14: Reference Board settings (layout, look, shrink-on-save) are global preferences; the arrangement itself
  is per document (saved in the .kra).
- 2026-11-13: autosaves of saved files keep Krita's `<file>.kra-autosave.kra` name (only untitled ones renamed).
