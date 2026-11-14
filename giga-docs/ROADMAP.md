# Giga Sketch — roadmap and status

Loaded at the start of every session (imported from CLAUDE.md). **Keep it current:** tick items, add
branches, and move things between sections as part of every merge. How the code works lives in
`NOTES.md`; this file is only *what's done, what's next, and what's been decided*.

Last updated: 2026-11-14

## Resume here
- **Next up:** user hand-tests `feature/reference-board` (not merged yet), then merge + push. See "Phase 5" below.
- Repo state: `fix/panel-snap` merged and pushed. `fix/taskbar-icon` has 1 unmerged commit (ask the user).
  Text tools are "okish"; don't push more text work.
- Workflow that works: propose approach + files → wait for OK → branch → build/run → self-test where possible →
  user tests by hand → merge `--no-ff` into `giga-main` → push → update this file and NOTES.md.
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
- [x] Colour panel, menus wording, New Document dialog (`feature/ui-color-panel`, `feature/ui-menus-wording`, …)
- [x] Space-saving mode, panel flyouts (`feature/ui-space-saving`)
- [x] Interface modes Studio / Photoshop / Procreate, F9 (`feature/interface-modes`, `feature/procreate-*`, `feature/studio-*`)
- [x] Panels float inside the window: tear-off by tab/title/strip, drag back to dock, per-mode memory (`feature/in-window-panels`)
- [x] Floating panels stay on top on tab changes; header buttons instead of ≡ menu (`fix/floating-panel-tabs`)
- [x] Mode switch: no stale layout restore, each mode keeps its floating panels (`fix/mode-switch-double-restore`)
- [x] Smooth panel dragging (no self-snap), snapping only to adjacent panels, Ctrl = free move (`fix/panel-snap`)
- [x] Save Dated JPG, Ctrl+Space zoom toward pointer, no brush-switch popup
- [ ] Nothing queued. Add UI requests here as the user reports them.

## Phase 5 — Feature blueprints (doc "Giga Sketch Feature Blueprints")
- [ ] Reference Board (`feature/reference-board`, built 2026-11-14, awaiting hand test): Normalize, Arrange (5 styles,
  order, shape, spacing), Board panel with own camera, Send to Board / Canvas, click-to-focus, ←/→, Ctrl+click colour,
  drop/paste, look options, Resample to Current Size + shrink-on-save. Self-test `refboard`.
- [ ] Annotations, Bubble Guess, Blob Brush, Harmony Wheel, 3D Model Layer: blueprinted only.

## Known issues / watch list
- If dragging a floating panel ever looks choppy again: run with `GIGA_PANEL_TRACE=1` and read the summary lines.
- After a Fedora update, link errors like `undefined reference to OpenColorIO_v2_4::…` / `Imf_3_2::…` = stale
  objects built against the old system library. Delete the `.o` files that mention the old namespace and rebuild
  (seen 2026-11-14: OCIO 2.4→2.5, OpenEXR 3.2→3.4).
- `panelsnap` self-test segfaults in a small test window (test dereferences a panel frame its drag docked).
- Pre-existing upstream test failures (not ours): TestSvgParser/Cloned/RoundTrip, SvgTextCursorTest RTL/vertical.

## Backlog / ideas (not scheduled)
- (empty — add user ideas here with the date)

## Decisions log
- 2026-11-13: brush feel tuning considered done (user tested).
- 2026-11-13: hyphenation → super low priority.
- 2026-11-13: text scale → low priority; text tools are "okish" for now.
- 2026-11-13: panel headers show Float/Dock and Close as buttons, not a dropdown (fewer clicks).
- 2026-11-14: Reference Board settings (layout, look, shrink-on-save) are global preferences; the arrangement itself
  is per document (saved in the .kra).
- 2026-11-13: autosaves of saved files keep Krita's `<file>.kra-autosave.kra` name (only untitled ones renamed).
