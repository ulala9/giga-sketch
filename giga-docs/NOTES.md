# Giga Sketch — code notes

Running notes on how the Krita codebase works, kept so they carry over between sessions.

## App identity and user paths (goal 1)
- `krita/main.cc` sets up the app's identity:
  - `KAboutData aboutData("krita", "Krita", ...)` (~line 718). The first argument is the *component name*. It becomes
    `QCoreApplication::applicationName()`, and **almost every user path comes from it**:
    - `KSharedConfig::openConfig()` with no argument → `~/.config/<appname>rc` (kritarc)
    - `QStandardPaths::AppDataLocation` → `~/.local/share/<appname>` (resources, bundles, resource DB, workspaces). Used ~38 times.
    - `CacheLocation` → `~/.cache/<appname>`
    - user-customized kxmlgui toolbars/menus → `~/.local/share/kxmlgui5/<appname>/`
  - Organization name is deliberately left empty (otherwise you get paths like `krita/krita`).
  - `app.setDesktopFileName("org.kde.krita")` (~line 805): Wayland app ID, used for taskbar grouping and the window icon.
  - Single-instance key: `"Krita5" + home path` (~line 246). If a second Krita uses the same key, it hands files to the first instance.
  - Translation domain is set separately with `KLocalizedString::setApplicationDomain("krita")`.
- Config files opened **by explicit name** (they don't follow appname). NOTE: Krita uses both .cpp and .cc — always grep both!
  - `kritadisplayrc` (QSettings, read early for OpenGL probing): `krita/main.cc`, `libs/ui/kis_config.cc`, `libs/ui/dialogs/kis_dlg_preferences.cc`, `libs/ui/opengl/kis_opengl.cpp`, `libs/ui/dialogs/KisDlgCustomTabletResolution.cpp`, `plugins/extensions/buginfo/dlg_buginfo.cpp`
  - `kritashortcutsrc`: `libs/widgetutils/kis_action_registry.cpp`, `libs/widgetutils/xmlgui/KisShortcutsEditor.cpp`
  - `kritarc`: `libs/ui/KoDocumentInfo.cpp`
- Usage log: `~/.local/share/krita.log` and `krita-sysinfo.log` (GenericDataLocation, hardcoded) — `libs/global/KisUsageLogger.cpp`.
- **Installed (read-only) data** goes to `<prefix>/share/krita/...` (about 113 CMake install rules hardcode `.../krita`).
  `libs/resources/KoResourcePaths.cpp` also looks in `getInstallationPrefix() + "share/krita/"` explicitly.
- Code that checks the app name as a string: `KisResourceStorage.cpp:414` (`== "krita"`),
  `kis_tool_options_popup.cpp:89` (`contains("krita")`), `KoToolBox.cpp:164`.
- The resource folder can be overridden by the `ResourceDirectory` key in kritarc (`KoResourcePaths::getAppDataLocation`).

## Branding
- CMake option `BRANDING` (top-level `CMakeLists.txt` ~line 384) picks a folder under `krita/pics/branding/<name>/`
  (default, Beta, Plus, Next). Qt6 builds pick "Beta" by default.
  Each folder holds `NN-apps-krita.png` icons, `sc-apps-krita.svgz` (splash logo, `:/krita-branding.svgz`), `krita.ico`, `branding.qrc`.
- Splash: `libs/ui/kis_splash_screen.cpp`; images in `krita/data/splash/` (banner.svg, artwork png). Links point to krita.org.
- About dialog: `libs/ui/dialogs/kis_about_application.cpp` (reads `KAboutData::applicationData()`).
- Desktop file: `krita/org.kde.krita.desktop` (Name, Exec=krita, Icon=krita, StartupWMClass=krita). AppStream: `krita/org.kde.krita.appdata.xml`.

## Giga identity changes (branch feature/isolate-identity)
- All Giga names live in `libs/global/GigaBranding.h` (header-only, inline functions — include it anywhere).
- App name `gigasketch` → user paths: `~/.config/gigasketchrc`, `~/.config/gigasketchdisplayrc`,
  `~/.config/gigasketchshortcutsrc`, `~/.local/share/gigasketch/`, `~/.cache/gigasketch/`,
  `~/.local/share/gigasketch.log`. Wayland app ID / desktop file: `io.github.ulala9.gigasketch`.
- Binary is still called `krita`, and installed data still lives in `install/share/krita/` (upstream CMake untouched).
- Gotcha: `KoResourcePaths::saveLocationInternal` appends an app folder to standard save locations
  that don't already end in the app name. Upstream hardcoded "/krita"; we use `GigaBranding::appName()`.
- `KoToolBox.cpp:164` compares the tool section with the app name, but sections look like "0 Krita/Shape"
  (capital K), so it never matched even upstream. Left alone.
- Autosaves (fix/autosave-name): never-saved documents autosave to `$HOME/gigasketch-<pid>-<doc>-autosave.kra`
  (`KisDocument::generateAutoSaveFileName`, uses GigaBranding::appName()); the startup recovery scan
  (`KisApplication::checkAutosaveFiles`) only matches `gigasketch-*` / `.gigasketch-*`. Matters because stock Krita's
  scan DELETES autosaves you decline in its recovery dialog. Saved documents still autosave next to the file as
  `<name>.kra-autosave.kra` (same as Krita, on purpose). Old `krita-*` Giga autosaves are no longer offered (File > Open).
  Self-test `autosave` (put a file in the scratch home first to check the scan; it reports whether the dialog opened).
- Not yet isolated: `kritacrash.log` (Windows only).

## Build dependencies (Fedora 44)
- Immer, Zug and Lager (header-only, by arximboldi) aren't packaged in Fedora. Build them into
  `GigaSketch/deps/prefix` and pass `-DCMAKE_PREFIX_PATH=.../deps/prefix` to cmake.

## Local setup
- Launcher: `~/.local/share/applications/io.github.ulala9.gigasketch.desktop` is a symlink to the one in
  `install/share/applications/`, so it updates on every `ninja install`. Run `kbuildsycoca6` if Plasma doesn't pick up a change.

## Branding (branch feature/branding)
- Default `BRANDING` is now `GigaSketch` (top-level CMakeLists.txt). Folder `krita/pics/branding/GigaSketch/`:
  SVG sources (`logo.svg`, `splash-background.svg`, `splash-banner.svg`) + `generate.sh` (ImageMagick) → PNG/JPG/SVGZ.
  `branding.qrc` maps `krita-branding.svgz` (app logo used everywhere) + `giga-splash/background.jpg` + `giga-splash/banner.svg`.
- `kis_splash_screen.cpp` uses the giga-splash images if they exist in the qrc, else falls back to Krita's.
- Welcome page (`KisWelcomePageWidget.cpp`): support/donation, krita.org link and the whole right column
  (`widgetRight`: news, updater, supporter badge) are hidden in code; `m_networkIsAllowed` forced false.
- Launcher icon uses a full path (install prefix isn't on the icon search path).
- Gotcha: some `#include` blocks in Krita files sit inside `#ifdef Q_OS_ANDROID` — put new includes right after the file's own header.

## Stroke pipeline (brush feel, goal 2)
- Tool → `KisToolFreehand` (libs/ui/tool/kis_tool_freehand.cc) → `KisToolFreehandHelper` (kis_tool_freehand_helper.cpp).
- **Threads:** the helper runs on the GUI thread, once per pointer event (`paintEvent` → `paint(info)`). It never paints
  pixels itself; it queues jobs with `m_d->strokesFacade->addJob(strokeId, new FreehandStrokeStrategy::Data(...))`.
  `FreehandStrokeStrategy` (libs/ui/tool/strokes/freehand_stroke.cpp) runs those jobs on worker threads and stamps dabs.
  Job types: a single dab (`paintAt`), a line (`paintLine`), or a cubic Bézier (`paintBezierCurve`: pi1, control1, control2, pi2).
  ⇒ Smoothing math happens on the GUI thread; keep it O(1)/O(small window) per event. Once a job is queued it's committed — no undo mid-stroke.
- Smoothing types: `KisSmoothingOptions::SmoothingType` (libs/ui/tool/kis_smoothing_options.h):
  NO_SMOOTHING=0, SIMPLE_SMOOTHING=1 ("Basic"), WEIGHTED_SMOOTHING=2, STABILIZER=3, PIXEL_PERFECT=4.
  Stored as an int in the main rc (`LineSmoothingType`); the combobox index == enum value (kis_tool_brush.cc).
  - Basic/Weighted: Gaussian-weighted average over recent points (Weighted only), then Bézier between points with tangents
    from neighbours (`paintBezierSegment`). Painting runs one segment behind the pen; `finishStroke()` paints the last segment.
    A 100 ms `strokeTimeoutTimer` also calls `finishStroke()` when the pen pauses.
  - Stabilizer: timer-driven (`stabilizerPollTimer`, interval = `stabilizerSampleSize` ms). Keeps a queue of N samples and
    paints their uniform average → lags behind the pen. `delayDistance` = "dead zone" radius. On pen-up, "Finish line"
    (`finishStabilizedCurve`) replays the last event N times so the line reaches the pen — all at once, no easing.
    `KisStabilizerDelayedPaintHelper` optionally spreads painting over time (20 ms timer).
  - Pixel: snaps to pixel centres and drops L-shaped jags.
- Smoothing UI: `plugins/tools/basictools/kis_tool_brush.cc` (`createOptionWidget`, `slotSetSmoothingType` shows/hides
  widgets per type). Shortcut actions via `addSmoothingAction(type, "action_id")`.

## Smooth Curve mode (branch feature/curve-smoothing)
- `KisSmoothingOptions::CURVE_SMOOTHING` = 5 (appended; combobox item "Smooth Curve"). Settings in the main rc:
  `GigaCurveSmoothingAmount`, `...SpeedInfluence`, `...PressureInfluence` (0..1), `GigaCurveSmoothingCatchUp` (bool).
- Math in `libs/ui/tool/KisCurveSmoother.{h,cpp}` (no Qt widgets/timers, unit-tested in `libs/ui/tests/KisCurveSmootherTest.cpp`,
  run: `ctest -R KisCurveSmoother` in build/):
  1. 1€ filter (Casiez 2012) on position. cutoff = minCutoff(amount) + beta(speedInfluence) * screenSpeed, times a pressure factor.
     minCutoff: 30 Hz (amount 0) → 0.5 Hz (amount 1), exponential. beta max 0.015 per screen px/s. Pressure/tilt are NOT smoothed.
  2. Spline points thinned to ≥1.5 screen px apart; centripetal Catmull-Rom → one cubic Bézier per segment
     (window of 4 points; phantom duplicate at start/end). Turns sharper than ~110° keep straight tangents (corners).
  3. Catch-up = keep feeding the last raw pen sample through the filter (exponential ease-out), then land exactly on it.
     Pen-up: done instantly in `end()` (geometry only). Pen resting: `curveRestTimer` (16 ms, GUI thread) calls `catchUpStep()`.
- Helper integration (`kis_tool_freehand_helper.cpp`): `curveStart/curveEnd/curvePaintSegments/curveRestTimeout`.
  In this mode `previousPaintInformation` = the smoothed line head (used for tap dab + outline); airbrushing feeds the raw pen pos.
- Tuning constants are at the top of KisCurveSmoother.cpp.

### Smooth Curve v2 (same branch)
- Filter fix: pen speed = movement of the *raw* pen ((raw - previousRaw)/dt), as in the original 1€ paper.
  v1 used (raw - filtered)/dt, i.e. the line's own lag, which raised the cutoff as lag grew and kept the
  line only ~8 px behind at 73% → felt like stock Krita. Max smoothing cutoff lowered to 0.3 Hz.
- Catch-up has a floor cutoff of 4 Hz (`CATCH_UP_MIN_CUTOFF`) so heavy Amount doesn't make it crawl.
- Animated tail on pen-up: `KisCurveSmoother::startTail/tailStep` (ease-out cubic over Catch-Up Time, default 120 ms).
  Brush outline during the tail: `paintOpOutline` uses the hover branch when `curveTailPending` (outline at the pen,
  not the tail); before this the cursor looked stuck for the whole Catch-Up Time after a lift.
  Helper keeps the stroke open: `endPaint()` → starts `curveTailTimer` (8 ms) → `curveTailTimeout()` → `endPaintImpl()`
  (the old endPaint body) when it lands.
  Safety: `isRunning()` is false while the tail runs (beginPrimaryAction asserts !isRunning); `finishPendingTail()` is
  called from initPaint/initPaintImpl, cancelPaint, slotSmoothingTypeChanged, the helper destructor, and
  KisToolFreehand::deactivate/requestStrokeEnd/requestStrokeCancellation/requestUndoDuringStroke (undo is otherwise
  refused while a stroke is open). Only animates when strokesFacade == resources->image() (canvas), never on the scratch pad.
- "Show Lag Line": `paintOpOutline()` adds `KisCurveSmoother::pendingPath()` (painted end → pending points → pen)
  to the brush outline. Needs "show outline while painting" on.
- New config keys: `GigaCurveSmoothingCatchUpTime` (ms), `GigaCurveSmoothingShowLagLine`.
- Undo during the tail (fix/undo-during-catch-up): KisDocument's UndoStack::undoImpl calls image->requestUndoDuringStroke(),
  then tryUndoUnfinishedLod0Stroke() (only works with instant preview/LOD), then tryBarrierLock() — and SILENTLY DROPS the
  undo if the image is still busy. finishPendingTail() closes the stroke but its dabs are still painting → Ctrl+Z lost.
  Fix in KisToolFreehand: "owed undo". requestUndoDuringStroke() with a pending tail records the document, connects
  (queued) to undoStack indexChanged (fires on push AND cumulative-undo merge; push happens on a worker thread), and a
  10 ms GUI timer waits for image()->isIdle(), allows one settle tick, then calls doc->undoStack()->undo() only if the
  stroke landed (LOD path pushes nothing → no double undo). Extra Ctrl+Z presses while busy are counted. Gives up after 3 s.

## Text tool audit (goal 3) — Krita 6.0.4.1
Code: tool `plugins/tools/svgtexttool/` (SvgTextTool, SvgTextCursor = on-canvas editing, Svg*Strategy = drag
interactions, Svg*Command = undoable edits, QML tool options `SvgTextToolOptions.qml`); engine `libs/flake/text/`
(KoSvgTextShape + `_p_layout`, `_p_glyphs`, `LayoutFunc_lines`, `LayoutFunc_inShape`; KoFontRegistry = fontconfig+FreeType;
KoOpenTypeFeatureInfoFactory); docker `plugins/dockers/textproperties/` (QML, **already has Paragraph + Character tabs**,
configurable visibility per property via TextPropertyConfigModel, CSS style presets = saved char/para styles).
Layout stack: HarfBuzz shaping, FreeType glyphs, Fontconfig lookup, libunibreak line breaks, bundled libraqm (bidi). Not Qt text layout.
Storage: text is an SVG <text> shape inside a vector layer (KisShapeLayer); `KraTextVersionId` handles old-file upgrades.

| Photoshop feature | Krita 6.0 | Notes |
|---|---|---|
| Font family / style / size | ✅ | FontFamily/FontStyle/FontSize.qml |
| Variable fonts | ✅ | FontAxesModel, FontVariationSettingsId, optical sizing |
| OpenType features | ✅ | ligatures, caps, numeric, position, East Asian, raw feature settings |
| Kerning | ✅ auto/none; manual = type-setting mode (drag glyphs, dx/dy) | no numeric per-pair kerning field |
| Tracking | ✅ letter-spacing | also word-spacing |
| Leading | ✅ line-height | |
| Baseline shift | ✅ | |
| Case / small caps / super-sub | ✅ text-transform, font-variant-caps/position, font-synthesis | |
| Faux bold / italic | ✅ FontSynthesisBold/Italic | |
| Underline / strike | ✅ text-decoration (style, colour, position) | |
| Horizontal / vertical scale | ❌ | only font-stretch (needs a wide-axis font) |
| Alignment / justify | ✅ text-align-all + text-align-last | |
| First-line indent | ✅ text-indent | |
| Left / right paragraph indents | ❌ | only shape-padding for text-in-shape |
| Space before / after paragraph | ❌ | SVG text has no paragraph boxes |
| Hyphenation | ❌ | no `hyphens` property |
| Point text | ✅ preformatted | |
| Box (area) text | ✅ inline-size wrap + text in shape | drag a rect when creating |
| Text on path | ✅ | click a path with the tool |
| On-canvas editing | ✅ SvgTextCursor (bidi, vertical, IME) | |
| Editable text layer | ⚠️ text lives in a vector layer; several texts share one layer | `createParentForShapes(shapes, forceNewLayer, …)` already has a flag |
| Separate Character / Paragraph panels | ✅ as tabs in the Text Properties docker | |
Baseline tests (stock 6.0.4.1 on Fedora 44): TestSvgTextShape passes; 4 fail before any Giga change —
TestSvgParser / Cloned / RoundTrip (mesh gradient + ICC refs) and SvgTextCursorTest (RTL/vertical word moves, font dependent).
Judge text work by "no new failures": `ctest -R "[Tt]ext|[Ff]ont|[Ss]vg"`.

## Text work done (branches feature/text-ps-workflow → feature/text-paragraph-spacing)
- **One layer per text** (tool option `gigaNewLayerPerText`, default on): `SvgCreateTextStrategy::createCommand` calls
  `createParentForShapes(..., forceNewLayer=true)` and stores the generated name in node property `gigaTextAutoName`.
  `SvgTextTool::updateAutoLayerName()` renames the layer to the text's first line when editing ends (cursor leaves the
  shape / tool deactivates) — only if the name still equals the stored auto name (user didn't rename) and the layer has
  one shape. Not an undo step. Gotcha: `KisShapeLayer` has two `name()`s (KisBaseNode + KoShape) → use `KisBaseNode::name()`.
- **Docker defaults:** `TextPropertyConfigModel::addProperty` makes PS core props AlwaysVisible while FollowDefault.
- **Paragraph spacing / indents:** PropertyIds `GigaSpaceBeforeId/AfterId/IndentStartId/IndentEndId` (qreal pt, block-only).
  Saved as `-giga-space-before` etc. in the `style` attribute (non-XML-attribute props go to style; see
  KoSvgTextContentElement ~line 351). **Stock Krita safety:** SvgStyleParser only keeps style names on its known lists
  (`styleAttributes/fontAttributes/textAttributes`), so unknown `-giga-*` are dropped silently. Do NOT route unknown names
  into `KoSvgTextProperties::parseSvgTextAttribute` — its final `else` is `qFatal` (crash).
  Layout: `breakLines()` shifts startPos/inlineSize for indents (anchor-aware) and adds `gigaParagraphGap()` to lineOffset
  after HardBreak; `flowTextInShapes()` adds the gap after HardBreak. Docker: `qml/ParagraphSpacing.qml` +
  model cursors `gigaSpaceBefore...` in `KoSvgTextPropertiesModel`. Tests: `libs/flake/tests/TestGigaParagraphSpacing.cpp`.
- Box text = drag a rect with the text tool → sets `inline-size` (wrapped text via breakLines), not shape-inside.

## Text options bar + moving (branches feature/text-options-bar → feature/text-easy-move)
- `plugins/tools/svgtexttool/GigaTextOptionsBar.{h,cpp}`: QToolBar on its own row (addToolBarBreak), one per main window
  (found by objectName "GigaTextOptionsBar"), shown in SvgTextTool::activate, hidden in deactivate.
  Reads/writes `KisCanvasResourceProvider::textPropertyData()` / `characterTextPropertyData()` — same data as the docker;
  KisTextPropertiesManager turns changes into undoable edits on the selection. Range selected → character data; otherwise
  paragraph data (whole text, or next-new-text defaults when nothing is selected). Colour = canvas foreground colour
  (SvgTextCursor::canvasResourceChanged applies FG colour to the selected text). Fonts from ResourceType::FontFamilies.
- Text tool click logic: `SvgTextTool::mousePressEvent` picks a strategy from `m_highlightItem`, computed on hover in
  `mouseMoveEvent`. Move zone was a thin ring (2x grab sensitivity) outside `outlineRect()`; now max(that, 24 screen px).
  Ctrl+drag → SvgMoveTextStrategy from anywhere when a text is selected.
- Move tool (`kis_tool_move.cc`) already moves vector/text layers via node offset (MoveSelectedLayer / MoveFirstLayer modes).

## Photoshop-style UI (branches feature/ui-* → merged into giga-main)
- Theme: `krita/data/themes/GigaDark.colors` (generated from KritaDarker, neutral greys). Defaults in `GigaBranding.h`:
  `defaultTheme()`, `defaultShortcutScheme()` = "photoshop_compatible" (file basename), `defaultInputProfile()` =
  "Photoshop Compatible". Used in KisApplication.cpp / thememanager.cpp / kis_action_registry.cpp /
  kshortcutschemeseditor.cpp / kis_input_profile_manager.cpp.
- Layout: `KisMainWindow::applyGigaLayout()` (Window > Workspace > Giga Sketch Layout; auto on first start = no
  "State" in [MainWindow]). Docker IDs: ToolBox, ColorSelectorNg, PaletteDocker, sharedtooldocker (only when tool options
  are in a docker), PresetDocker, TextProperties, KisLayerBox, ChannelDocker, History.
- Options bar: `KisConfig::toolOptionsInDocker` default now false (Tool Settings popup button in the BrushesAndStuff
  toolbar). `KisPaintopBox::gigaUpdateBarForTool()` hides brush-only toolbar actions for non-paint tools
  (paint tool = KisTool flag FLAG_USES_CUSTOM_PRESET).
- Toolbox groups: table `gigaToolGroups()` in `libs/ui/toolbox/KoToolBox.cpp` (tool IDs → group/section). One visible
  button per group (last used, config KoToolBox/gigaGroupLast_<key>), DelayedPopup menu + right-click event filter,
  corner triangle in `KoToolBoxButton::paintEvent`. Toggle: KoToolBox/gigaGroupedTools (toolbox context menu).
  `setButtonsVisible()` records Krita's visibility in `codeVisible` then `gigaApplyGroups()` hides non-current members.
- Panel strip: `KisMainWindow::createGigaPanelStrip()` vertical QToolBar "GigaPanelStrip" (RightToolBarArea); toggles
  docks docked (Wayland can't position floating windows). Collapse-all list in [MainWindow] gigaCollapsedPanels.
- Tab tear-off: `KisMainWindow::eventFilter` on the MDI tab bar; >40 px off the bar → `gigaDetachActiveView()`
  (new KisMainWindow + addViewAndNotifyLoadingCompleted, then close old subwindow).
- Already in Krita (no change): scrubby sliders (KisSliderSpinBox drag), layer docker button row, docker tear-off by
  dragging title bar/tab.
- NEVER screenshot the desktop to verify UI (captured a private window once). Ask the user to look.

## Photoshop-style panels, menus, New Document (feature/ui-layers-panel … feature/ui-new-document → giga-main)
- Layers docker (`plugins/dockers/layerdocker`): `NodeDelegate::Private::rightmostProperties()` filters to "on"
  properties + warnings when config `gigaCleanLayerRows` (default true). `LayerBox::gigaSetupPanel()` adds the Lock row
  (toggle_layer_alpha_lock / toggle_layer_inherit_alpha / toggle_layer_lock actions) into `verticalLayout` index 2 and
  rebuilds `hbox1`: new + duplicate (Krita's own bnDuplicate, `feature/layers-duplicate-button`) + delete inserted at the front (left, before the .ui spacer), fx/mask/fill-adjust/group appended (right). Eye column was already on the left (VISIBILITY_COL → 0).
- Colour: `WGConfig::defaultColorSelectorConfiguration` = Square + Slider (SV + H); commonColors off. Layout colour group
  = WideGamutColorSelector + SpecificColorSelector + PaletteDocker.
- Menus: `krita/gigasketch.xmlgui` (generated from krita5.xmlgui; version `GIGA_XMLGUI_VERSION` in krita/CMakeLists.txt,
  added to krita.qrc). Loaded in KisMainWindow ctor instead of krita5.xmlgui. Keep `<Menu name="tools">` (Python uses
  tools/scripts). The Window menu must stay `<Menu name="settings">`: KToolBarHandler (ktoolbarhandler.cpp) always
  merges "Toolbars Shown" into a menu named "settings" and creates an unlabeled "No text" menu if none exists.
  Taskbar icon: main.cc sets app name + RESOURCE_NAME before KisApplication → WM_CLASS gigasketch. Labels renamed in `KisMainWindow::gigaApplyWording()` (action name → text).
- New Document: `libs/ui/dialogs/GigaNewDocumentDialog.{h,cpp}`; slotFileNew uses it unless `gigaPsNewDocument=false`.
  Creates via `KisDocument::newImage(..., RASTER_LAYER, ...)` + `KisPart::startCustomDocument`.

## Space-saving + Procreate mode (feature/ui-space-saving, feature/procreate-mode → giga-main)
- Panel flyouts: `KisMainWindow::gigaShowPanelFlyout(id, anchor)` borrows `dock->widget()` into a Qt::Popup frame
  (GigaPanelFlyout) and gives it back on hide. Strip click = flyout, Shift+click = dock. Sizes in [MainWindow][GigaFlyoutSizes].
- `gigaMakePanelsNarrowFriendly()` wraps listed docks' content in a QScrollArea (min width 120). `gigaApplyCompactMode()`
  (View > Compact Interface, `gigaCompactUi`). Keys F5 Brushes / F7 Layers / F8 Color. Canvas-only (Tab) defaults keep
  menu/title/status bars.
- Input profile `krita/data/input/gigasketch.profile` ("Giga Sketch", default): Shift+drag = brush size, hold S = line.
- Separate eraser: `KisPaintopBox::gigaSwapEraserBrush()`, config keys gigaBrushPaint*/gigaBrushEraser* (+ Smudge in
  Procreate mode). `gigaSeparateEraser=false` = Krita's global toggle.
- Procreate mode: `libs/ui/GigaProcreateMode.{h,cpp}` (F9 — user has F12 = canvas only, `gigaProcreateMode`, Studio state in `gigaStudioState`).
- Testing tip: a second Giga instance hands off to a running one (single-instance key uses $HOME); for smoke tests use a
  temp HOME + XDG_* dirs. TERM-killed runs don't flush config.

## QuickShape (feature/quickshape → giga-main)
- `libs/ui/tool/KisQuickShapeRecognizer.{h,cpp}`: line → closed (polygon/rectangle via RDP corners, else PCA ellipse) →
  open (polyline via corners, else Kasa circle-fit arc). Thresholds at top of the .cpp. Tests: KisQuickShapeRecognizerTest.
- Helper: `initPaint` records args + starts recording; `paint()` appends raw points and restarts `quickShapeTimer` when
  the pen moves > 3 screen px; `quickShapeTimeout()` recognises, stops smoothing timers, `cancelStroke` (rolls back paint),
  `initPaintImpl` (with `quickShapeRepainting` so smoothing doesn't restart) and paints the QPainterPath elements via
  paintLine / paintBezierCurve at the median pressure. After a snap, `paint()` ignores input until endPaint.
- Config: `gigaQuickShape` (bool), `gigaQuickShapeDelay` (ms). Shift at snap time = perfect.
- No automated test drives the helper's cancel/repaint path; verify by hand (incl. multibrush, LoD on large canvases).

## Interface modes (feature/interface-modes → giga-main)
- `libs/ui/GigaInterfaceModes.{h,cpp}`: F9 cycles Studio(0) → Photoshop(1) → Procreate(2); View > Interface Mode.
  Per-mode window state in `gigaModeState<n>`; current mode `gigaInterfaceMode` (migrates old `gigaProcreateMode`).
  Created in KisMainWindow::createGigaPanelStrip(); `restoreSavedMode()` runs in the post-construction singleShot.
- Photoshop mode = `setPhotoshopPanels(true)`: tabs North, dockOptions incl. GroupedDragging, CSS on `QMainWindow > QTabBar`,
  and `GigaPanelTitleBar` (libs/ui/GigaPanelTitleBar.*) as each dock's title bar (originals kept in a QHash and restored).
  Collapse = hide dock->widget() + maxHeight = header.
- GigaProcreateMode now only shows/hides bars (`setActive`, `hideBars`); takes the "Next Interface Mode" action for its Actions menu.
- Studio = "classic Krita" (feature/studio-krita-mode): `KisMainWindow::gigaSetClassicInterface(bool)`.
  - Menu file per mode: Studio loads `krita5.xmlgui` (user's local copy in `~/.local/share/gigasketch/` wins, version 543),
    others `gigasketch.xmlgui`. Chosen in the ctor from `GigaInterfaceModes::savedMode()`; switched at runtime by
    `gigaLoadMenuFile` = removeClient → setXMLGUIBuildDocument(empty) → setLocalXMLFile/setXMLFile → addClient (same as
    Configure Toolbars). Afterwards re-add View menu items (`gigaAddViewMenuItems`), `synchronizeDynamicActions()` (Python
    menu actions), `applyToolBarLayout()`. Never keep QMenu pointers from xmlgui containers (Procreate's Adjustments looks
    the Filter menu up on aboutToShow).
  - Studio also: Krita wording (originals saved in `gigaKritaWording`), panel strip hidden, compact margins restored.
  - Theme per mode: `gigaModeTheme<n>` (Studio default "Krita blender", others "Giga Dark").
  - Startup bug fixed: restoreSavedMode used to run setMode from Studio, saving the just-restored (other mode's) layout
    into gigaModeState0. Now `switchTo(mode, atStartup=true)` skips save/restore (Krita already restored the right State).
  - User's Studio layout seeded from the flatpak Krita kritarc `[MainWindow] State` (backup: gigasketchrc.before-studio-krita).
- Bars fix + polish (feature/mode-bars-messages):
  - KMainWindow::applyMainWindowSettings (run on every document open via setAutoSaveSettings, and in finalizeGUI after
    a menu rebuild) re-applies [MainWindow] State/MenuBar/StatusBar from config. Procreate's hidden bars got saved there.
    Fix: `syncSavedWindowSettings()` writes current State/MenuBar/StatusBar after every mode switch;
    `showStandardBars()` in Studio/Photoshop; Procreate remembers and re-shows the toolbars it hid.
  - Mode switch shows a canvas floating message ("Photoshop mode") via KisViewManager::showFloatingMessage (needs a doc).
  - Photoshop mode drop indicator: Qt's dock gap indicator is a QRubberBand child of the main window
    (QMainWindowLayout::gapIndicator); caught on ChildAdded, painted blue in an eventFilter.
  - Upstream bug guarded: KisShadeSelectorLine::resizeEvent divided by old width 0 (SIGFPE) when a layout floats the
    Advanced Color Selector.
  - Status bar per mode: `gigaModeStatusBar<n>` (Studio off, Photoshop on), also sets KisConfig showStatusBar + the showStatusBar action (Krita re-applies that on document open).
  - Welcome page (no document): Krita hides all panels and never saves the layout there (`saveWindowState`,
    `adjustLayoutForWelcomePage`). `KisMainWindow::gigaShowingWelcomePage()`; on the Welcome page switchTo() works with
    the SAVED layouts (leaving mode <- [MainWindow] State; entering mode -> written to [MainWindow] State, applied when a
    document opens) and only records MenuBar/StatusBar. Before this fix the startup sync saved the hidden-panel layout
    over the user's real one ("layout resets after restart").
  - Mode switch speed (measured with temporary timers): never setStyleSheet on the QMainWindow (repolishes every widget,
    ~400 ms each way). Photoshop styling = style sheet on the panel QTabBars only (direct children of the window, found
    again on ChildPolished); headers created only for visible docks (others on first visibilityChanged) and kept/reused.
    Use ChildPolished, not ChildAdded: at ChildAdded the child isn't constructed, so qobject_cast fails (this had broken
    the blue drop indicator). Remaining cost: theme swap (Krita's updateTheme reloads all icons, ~0.5-0.8 s) and the
    Studio menu rebuild (~0.1 s). setUpdatesEnabled(false) around the switch avoids partial redraws.
  - "Same Theme in All Modes" (View > Interface Mode, `gigaOneTheme`, default on): applyModeTheme() does nothing and
    leaving a mode doesn't overwrite its `gigaModeTheme<n>`; unticking re-applies the current mode's own theme.
  - Smoke-test gotcha: config's ResourceDirectory is absolute; point it at the temp HOME or the test waits on the running app's lock.

## Text boxes (feature/text-boxes → giga-main)
- Drag with the text tool = Photoshop box: text flows inside an invisible KoPathShape rectangle added as SVG 2
  `shape-inside` (KoSvgTextAddShapeCommand). Click = point text. Tool option "Drag Creates a Text Box"
  (`gigaBoxText`, SvgTextToolOptionsData/Model/QML); off = Krita's inline-size (width-only) text.
- `plugins/tools/svgtexttool/GigaTextBoxResizeStrategy.{h,cpp}`: `GigaTextBox::boxOf()` (one inside shape, a KoPathShape,
  translate-only transform, ≤6 path elements), handle math, `createBoxShape()`, resize strategy (Shift = keep ratio,
  Alt = from centre, min size ≈ font size), ResizeCommand (undo), RemoveBoxCommand (Convert to Point Text; owns the box).
- SvgTextTool: HighlightItem::BoxHandle / DragMode::BoxResize; handles painted after the selected-shape block (separate
  KisHandlePainterHelper scope); red ⊞ overflow marker at the bottom-right handle; decorationsRect grown for both.
  Right-click (`popupActionsMenu`) = Convert to Point Text / Convert to Paragraph Text (not for text on a path).
- Flake additions: `CharacterResult::gigaOverflow` (set where LayoutFunc_inShape hides words that don't fit),
  `KoSvgTextShape::gigaTextOverflows()`, `gigaContourChanged(shape)`.
- GOTCHA: `KoPathShape::setSize()` notifies dependees BEFORE mapping its points, so the text relayouts with the old box.
  Always call `text->gigaContourChanged(box)` after resizing a contour with setSize.
- Tests: libs/flake/tests/TestGigaTextBox (7). SvgTextCursorTest has 8 pre-existing failures (RTL/vertical word moves).
- Known small leak: a box from an undone "create box text" with no redo stays parentless until exit.

## Font picker (feature/font-picker → giga-main)
- `plugins/tools/svgtexttool/GigaFontPicker.{h,cpp}`: `GigaFontComboBox` (QComboBox whose showPopup() emits
  pickerRequested) + `GigaFontPicker` popup (Qt::Popup QFrame): search field (contains, case-insensitive; top match
  pre-selected and previewed), sections Favourites / Recent / All Fonts, rows = ☆/★ + name + preview.
- Previews = the KoFontFamily resource image (black "AaBbGg"-style sample on white 256x256), cropped and tinted to the
  palette text colour (`tintPreview`), cached per family.
- Settings: KConfig group `GigaFontPicker`, keys `recent` (max 8, newest first) and `favourites`.
- Live preview: GigaTextOptionsBar applies the hovered family through the provider; consecutive edits merge into one
  undo step (SvgTextMergePropertiesRangeCommand::mergeWith). Disabled when the selection mixes fonts. Cancel (Esc /
  click away) re-applies the original family list → may leave one no-op undo entry.
- Test: plugins/tools/svgtexttool/tests/GigaFontPickerTest (6).

## Character / Paragraph panels (feature/character-panel → giga-main)
- `plugins/tools/svgtexttool/GigaTextPropertyAccess.{h,cpp}`: shared read/apply of text properties via the canvas
  resource provider (textPropertyData / characterTextPropertyData), font live preview, font lists/styles, and unit
  conversions: tracking (1/1000 em) <-> LetterSpacingId (AutoLengthPercentage, em), leading (pt, <0 = Auto) <->
  LineHeightId (LineHeightInfo), baseline shift (pt) <-> BaselineShiftMode/Value. GigaTextOptionsBar now delegates to it.
- `GigaCharacterPanel`: family (GigaFontComboBox + picker), style, size, leading, kerning (Metrics=AutoValue auto,
  0=AutoValue(0)), tracking, baseline, colour, toggles (weight 700, italic, TextTransform uppercase, FontVariantCaps
  small, FontVariantPosition super/sub, TextDecorationLine underline/line-through), anti-aliasing.
- `GigaParagraphPanel`: 7 align buttons (justify variants = TextAlignAll justify + TextAlignLast), Giga indents/spaces,
  first line = TextIndentId (eachLine=true).
- `GigaTextPanelDock` + factories ("GigaCharacter", "GigaParagraph", DockMinimized) registered in the tool plugin's
  Plugin ctor (tool plugins load before the main window creates docks). Options bar Panels button tabs+shows them;
  applyGigaLayout puts them in the Tool Options/Presets group.
- Not supported by the engine (no property): character scale, hyphenation.
- Tests: tests/GigaTextPanelsTest (9) uses KisCanvasResourceProvider(nullptr) + a bare KoCanvasResourceProvider.

## Save Dated JPG (feature/save-dated-jpg → giga-main)
- Built-in replacement for the user's PyQt5 "Save Dated JPG" Python docker (PyQt5 isn't available in this Qt6 build).
- `libs/ui/GigaSaveDated.{h,cpp}`: `nextDatedPath()` (yyyy-MM-dd.jpg, then _v2, _v3...) and `saveDatedJpg()`: folder =
  the .kra's folder; never-saved image asks once (`gigaSaveDatedFolder`). Export via KisDocument::exportDocument (async,
  like File > Export) with the last-used image/jpeg config at quality 90.
- GOTCHA: KisImportExportManager::askUserAboutExportConfiguration opens the JPEG options dialog unless the document is
  in file batch mode. Set `setFileBatchMode(true)` only around the exportDocument() call (read synchronously at start).
- Action `giga_save_dated_jpg` is defined in `krita/gigasketch.action` (installed with Krita's .action files) so it
  appears in Configure Shortcuts and keeps custom shortcuts; created via actionManager->createAction();
  `KisMainWindow::gigaAddFileMenuItems()` inserts it after file_save_as (also after menu rebuilds).
- Test: libs/ui/tests/TestGigaSaveDated.

## Ctrl+Space zoom toward pointer (feature/zoom-toward-pointer → giga-main)
- KisZoomAction modes: ZoomModeShortcut(0) zooms around the canvas centre; RelativeZoomModeShortcut(7) zooms toward the
  press point (like the wheel); Discrete(1) / RelativeDiscrete(8) the same for stepped zoom.
- Default gigasketch.profile now uses 7 for Ctrl+Space drag and 8 for Ctrl+Alt+Space drag (Krita default did too).
- `libs/ui/GigaZoomOption.{h,cpp}`: View > "Ctrl+Space Zooms Toward Pointer" flips the Space-key zoom bindings of the
  current profile (`modeFor()`, tested) and calls KisInputProfileManager::saveProfiles() (writes the user's copy in
  data/input/, emits currentProfileChanged so the canvas reloads bindings). The checkbox follows currentProfileChanged.
- KisInputProfile / KisShortcutConfiguration aren't exported from kritaui, so tests only cover modeFor().

## Brush-switch popup (fix/no-brush-switch-popup)
- The "<preset>\nselected" floating message (with preset thumbnail) comes only from KisPaintopBox::slotPreviousFavoritePreset,
  slotNextFavoritePreset and slotSwitchToPreviousPreset (shortcut switching). Now guarded by
  KisConfig::gigaShowBrushSwitchMessage() (key `gigaShowBrushSwitchMessage`, default false).
- Preferences > General > Window: checkbox added in code under "Show on-canvas popup messages" (QFormLayout::insertRow
  after m_chkCanvasMessages; enabled only while that one is on), so the .ui file stays untouched.
- KisFloatingMessage is a top-level window positioned in global coordinates (top-right of the canvas); on Wayland it can
  land elsewhere (user saw it top-left).

## In-window floating panels (feature/in-window-panels) — spike results (2026-11-13)
- Spike (env-gated, removed): removeDockWidget(dock) + dock->setParent(QFrame child of KisMainWindow) + layout->addWidget.
  Result: dock stays visible, content visible (LayerBox's isVisible() gate keeps working), isWindow()=false (no Wayland
  top-level), dockWidgetArea = NoDockWidgetArea.
- QMainWindow::saveState() simply omits a dock that lives in such a frame. restoreState() of the current state AND of an
  older state where that dock was docked both LEAVE it in the frame (Qt doesn't pull it back). So re-docking after a
  layout/mode restore is our job (manager), and restoring is not destructive.
- Running a second instance for tests: the single-instance key uses $HOME, so use HOME=<scratch>/home and
  XDG_CONFIG_HOME=<scratch>/cfg. Krita's message handler hides qWarning output here; print with fprintf(stderr).
- Each SPIKE line printed twice at startup: check whether two KisMainWindows are constructed (manager must be per window).
- Not yet verified: painting lag with a panel over the OpenGL canvas (needs the user's hand test).
- Step 2+3 built (same branch). `libs/ui/GigaFloatingPanel.{h,cpp}`: QFrame child of KisMainWindow holding the whole
  QDockWidget; the dock's own title bar is the drag handle (frame's eventFilter on the dock eats title-area presses so
  QDockWidget never starts its own drag; Movable+Floatable removed while inside), 5 px border resizes, double-click
  title / Photoshop header collapse → frame collapse, LayoutRequest → ensureTitleBar() (Krita hides title bars).
  `beginInteractiveMove()` grabs the mouse for tear-offs.
- `GigaFloatingPanelManager` (one per window, `KisMainWindow::gigaFloatingPanels()`): strips DockWidgetFloatable from
  every dock while enabled (so Qt never makes desktop windows); float/dock/dockAll/toggleFloat/raisePanel; keepInside +
  snap (window area, central widget, other panels, 10 px); qApp filter raises a panel on press; follows dock
  ShowToParent/HideToParent; setAllHidden (toggleDockersVisibility + leaving Welcome page in
  setMainWindowLayoutForCurrentMainWidget); begin/endRestore around restoreWorkspaceState (it hides every dock);
  JSON per mode in kritarc `gigaModeFloatingPanels<mode>` (GigaInterfaceModes::switchTo: save+dockAll when leaving,
  setStorageKey+restoreSaved when entering/at startup). Setting `gigaInWindowPanels` (Preferences > General > Window).
- Snapping (fix/panel-snap, 2026-11-14): `snap(r, self)` must skip the dragged panel by identity -- mid-drag its
  geometry is last frame's, so a geometry compare let it snap to itself (~11 px steps, "moves on a grid"). Other
  panels' x-edges only count if the panels overlap vertically (±SNAP_DISTANCE), y-edges if they overlap horizontally.
  Ctrl held = no snap (QGuiApplication::keyboardModifiers). Self-test `panelsnap` (Ctrl not testable there).
- KoDockWidgetTitleBar::setFloatRequestHandler (libs/widgets, additive): float button visible when a handler exists,
  click → handler first. Photoshop header's Float/Dock menu item goes through the manager.
- Flyout: drag title row (GigaPanelFlyout::onTearOff) or pin button → floating panel; strip buttons have property
  gigaPanelId + KisMainWindow::eventFilter → drag out = float + beginInteractiveMove. Strip click on a floating panel =
  raise, Shift+click = dock.
- Self-test (temporary, removed) passed: float/collapse/hide-all/workspace restore/close/raise/dock/disable/re-enable,
  restart restores position. Only one KisMainWindow at startup (step-1 double print was a test artefact).
- Tear-off by title bar / tab (user report: dragging a docked panel's title "snaps back" — Qt can't drop a non-floatable
  dock on the canvas). Manager filters each watched dock (title-area press) and every QTabBar child of the window
  (dock via tabData = QDockWidget address, checked against m_watched; fallback by title text). Qt keeps its own docking
  drag; when a move lands >30 px inside centralWidget(), tearOff(): send a synthetic MouseButtonRelease to the source
  (Qt ends its drag and restores the dock), release any grabber, floatPanel() under the pointer, beginInteractiveMove().
  Fallback: a release seen over the canvas without moves → float there after Qt finishes (singleShot 0).
- Raise-on-press is a separate tiny app-wide filter (GigaRaiseOnPressFilter, presses only); the manager itself only
  filters objects it watches (avoid running drag logic twice / per-event casts while painting).
- Synthetic-event self-test (removed) passed: live tear-off follows the pointer, fallback floats, short drag near the
  docks still re-docks via Qt (Qt put it in the top area in that synthetic case — Qt's own hover logic).

### In-window panels, step 4: drag a floating panel back into the docks
- `GigaFloatingPanel::setMoveObserver()` reports every move step and the release point; the manager's `onPanelMoved()` turns the pointer position into a `DropTarget` (`dropTargetAt()`):
  1. pointer over a docked panel (or its group's tab row) → `tabifyDockWidget` with it (ToolBox is excluded: too slim to share a group);
  2. pointer within 24 px of the canvas's left edge (or anywhere left of it) → own column right of the ToolBox (`splitDockWidget(toolBox, dock, Horizontal)`) so the toolbox stays slim;
  3. within 24 px of the right edge (or right of it) → `addDockWidget(Right, dock, Vertical)`, stacked under the right-side panels.
- A `GigaDropHighlight` child widget (mouse-transparent, palette highlight at 60 alpha) shows the target while dragging; the dragged panel is raised above it.
- Hold **Ctrl** while dragging to move without docking.
- The actual docking is queued (`QTimer::singleShot(0)`) because the observer runs inside the panel's own mouse handler. `dockPanel()` (button/menu) and drops share `dockInto()`.
- 24 px side zone is deliberately < the 30 px tear-off zone so a just-torn-off panel doesn't immediately re-dock.
- Only runs during panel drags (not painting); cost per move is a loop over ~30 docks.
- Gotcha: Qt can report a docked panel as `isVisible()` while it sits parked outside the window at a stale position (seen with Photoshop-mode groups). `dropTargetAt()` only counts docks whose rect is inside the window and doesn't touch the canvas, and only trusts a tab bar that sits directly on its panel. Before that, the right-side column union grew to almost the whole window, giving a huge highlight.

### In-window panels: buried by tab changes (fix/floating-panel-tabs)
- Bug: clicking a tab in a docked panel group hid every floating panel (they came back when painting created a child
  widget → m_raiseTimer). Cause is in Qt: `QMainWindowLayout::tabChanged()` calls `centralWidget()->raise()`.
  The panels were still "visible", just under the canvas — tests must check `childAt(panel centre)`, not isVisible().
- Fix: the manager watches the central widget (installed on first float) and on `QEvent::ZOrderChange` calls
  raiseAllFrames() synchronously. Raising siblings doesn't send ZOrderChange to the canvas, so no loop.
- Photoshop header (`GigaPanelTitleBar`): the ≡ menu is gone; Float/Dock (docker_float icon) and Close (docker_close,
  Shift+click = close tab group) are direct buttons. The old menu is on right-click of the header.
- "Crash after Photoshop → Studio + tab drag" was a SELF-TEST ARTEFACT (fix/mode-switch-tab-crash): Qt retires old
  panel-group tab bars with deleteLater(); inside the self-test's timer callback processEvents() doesn't run deferred
  deletes, so the test dragged an already-retired bar → segfault in QDockAreaLayoutInfo::tabIndexToListIndex.
  `pump()` now also calls sendPostedEvents(nullptr, QEvent::DeferredDelete). Test `modecrash` covers mode switches +
  reorder + tear-off in both modes.
- Learned on the way: a mode switch restores the layout TWICE. gigaLoadMenuFile → guiFactory()->addClient →
  KXmlGuiWindow::finalizeGUI → applyMainWindowSettings → restoreState([MainWindow] State = the mode being left), then
  switchTo restores the new mode's state. Harmless (old tab bars are retired) but wasted work; a candidate if mode
  switching ever needs to be faster (hide the State entry during addClient — note setAutoSaveSettings() itself
  re-applies the settings, so don't use reset/setAutoSaveSettings for that).
  FIXED (fix/mode-switch-double-restore): `KisMainWindow::applyMainWindowSettings` override; while
  `gigaLoadMenuFile` runs `addClient()` (flag `gigaSkipSavedLayout`) it passes an in-memory copy of the group
  (`KConfig(QString(), SimpleConfig)` + `copyTo`, which also copies the Toolbar subgroups) without "State". Measured:
  the stale State was actually applied TWICE per Studio switch. Speed gain not measurable in the self-test config
  (~50 ms per Studio switch before and after); the win is no stale intermediate layout. Self-test `modeswitch`
  (all mode pairs: no extra restore, same layout signature on re-entry, Procreate hides bars).
- Floating panels lost after cycling modes (user report, same branch): leaving a mode ran saveNow() then
  dockAll(false); dockPanel() schedules a save (400 ms timer), and the next mode's setStorageKey() flushes pending
  saves under the OLD key first -> wrote `[]` over the list just saved. Fix: `GigaFloatingPanelManager::saveAndPark()`
  (save, dock hidden, stop the timer), used by GigaInterfaceModes::switchTo. Self-test `modefloat`.
- Qt 6.11 internals: QMainWindowLayout::applyState retires tab bars not in the new state (unuseTabBar = deleteLater);
  restoreState ends with showTabBars() (shows every used bar). tabChanged() raises the central widget.

## Reference Board (feature/reference-board)
- Blueprint: Claude doc "Giga Sketch Feature Blueprints" → Reference Board (claude.ai/artifact/KkhoZFGHU3vzMt7tmZ8NTK).
- Krita side: each reference = `KisReferenceImage` (a KoShape) in one hidden per-document `KisReferenceImagesLayer`;
  `KisReferenceImagesDecoration` paints the layer's shape manager over the canvas; the tool is
  `plugins/tools/defaulttool/referenceimagestool/`. Saved by `kis_kra_savexml_visitor` (saveXml) + `kis_kra_save_visitor`
  (saveImage); `.krf` sets use the same saveXml/fromXml.
- "On the Board" = `KisReferenceImage::setOnBoard(true)` = `setVisible(false)`: hidden shapes are skipped by canvas paint,
  `shapeAt`/`shapesAt` and `KoSelection::select`, so nothing else needed changing (plus `selectAll` skips hidden ones).
  The canvas transform is never touched while on the Board, so Send to Canvas is exact.
- New per-image fields: `boardRect` (document points, Board coordinates). XML: optional `giga-onBoard`, `giga-boardRect`
  ("x y w h"); stock Krita ignores them (images show at their canvas spot). Loading only puts an image on the Board if
  it has a valid boardRect.
- `libs/ui/GigaReferenceLayout.{h,cpp}`: pure maths (normalize modes, Justified rows / Masonry / Packed (skyline) /
  Grid / Auto, score = fill × aspect fit, readingOrder). Scaling styles re-run once or twice so total image area
  stays the same (otherwise a lone image in a row gets stretched). Unit test `TestGigaReferenceLayout` (38 cases).
- `libs/ui/GigaReferenceBoard.{h,cpp}`: `GeometryCommand` (before/after {transform, onBoard, boardRect} per image —
  one class for normalize/arrange/send/move), `ResampleCommand` (keeps old QImage for undo), `Notifier` (changed() on
  every apply, settingsChanged()), helpers, `Settings` (KisConfig keys `gigaRefBoard*`, global not per document),
  `animateOnCanvas` (200 ms lerp of transforms after the command is pushed; stops if any other Board command applies
  or someone else moves the shape; snaps survivors to the final state).
- Resample rule: target width = max(canvas width at 100 % in image px, Board focus width) × 1.5; only shrinks, and only
  if < 90 % of current. Board size comes from `setBoardViewportSize()` (atomics, read while saving).
  "Shrink oversized references when saving" (Board ▸ Look) scales only the saved PNG in `saveImage()`.
- Panel: `plugins/dockers/gigareferenceboard/` (docker id `GigaReferenceBoard`, DockMinimized, in GIGA_PANELS strip).
  `GigaReferenceBoardView`: camera widget = board × zoom + pan; auto-fit until the user moves the camera; focus fly-to
  300 ms (zoom geometric, centre linear); board rects animate 200 ms on change; draws with
  `KisReferenceImage::paintInRect` (same mipmaps as the canvas). Takes keyboard focus on hover (gives it back on leave,
  never from text fields); ShortcutOverride for Esc/Home/←/→/Space/Ctrl+V. Touch: 1 finger = mouse, 2 = pinch/pan;
  touchpad: NativeGesture zoom, pixelDelta scroll pans.
- Hand-dropped image: drawn at `m_dropRect` until its move command lands and excluded from the change animation
  (else it glided from the old spot = "rubber-banding", user report). Pressing on the Board stops camera fly-tos.
- Tool's Send to Board shows the Board dock if hidden.
- Self-test `refboard` (14 checks, incl. `board-drop-no-rubberband`). Q_PROPERTY imageCount/focusedIndex + Q_INVOKABLE imageWidgetCenter exist for it.
- Known: the old `panelsnap` self-test segfaults in a small test window (948×512) even without this feature — its
  History frame gets docked by the drag and the test dereferences the deleted frame (test bug, not app).

## UI self-tests (dev hook)
- `giga-tools/selftest.sh <name|all>` runs Giga Sketch in a throw-away HOME/XDG_CONFIG_HOME with `GIGA_SELFTEST=<name>`, prints only `GIGA_SELFTEST PASS/FAIL/INFO/DONE` lines, exit code = failures.
- Code: `libs/ui/GigaSelfTest.cpp` (called from the KisMainWindow ctor via `GigaSelfTest::maybeRun`; no-op without the env var). Helpers: `report()`, `info()`, `pump()`, `drag(widget, localStart, globalEnd, steps, beforeRelease)`. Add a test function and a row in `runNamed()`'s table. `GIGA_SELFTEST_DELAY` (ms, default 5000) delays the start.
- Ends with `_Exit(failures)` to skip Krita's shutdown prompts.
- Existing tests: `panels` (float/dock, drop onto Layers, left/right edge, middle, title-bar tear-off);
  `tabswitch` (opens a document — the Welcome page hides all panels — floats History by API / tab drag / title drag in
  Studio then Photoshop, clicks every group tab, checks the panel is on top). `GIGA_MODE=<0|1>`, `GIGA_HOW=<0|1|2>` pin it.
- The `drag()` helper sends events to `QWidget::mouseGrabber()` when there is one (like Qt does).
- `pump()` flushes deferred deletes too (see the modecrash note) — keep it that way or tests see dead widgets.
- `panelmove` times panel drags; `GIGA_PANEL_TRACE=1` (any run) prints a summary line per real panel drag.
- Gotcha: a non-front tab in a dock group keeps a stale position; `raise()` it before aiming at it.
