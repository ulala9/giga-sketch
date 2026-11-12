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
- Not yet isolated: autosave files `krita-*-autosave.kra` (same pattern and folder as stock Krita);
  `kritacrash.log` (Windows only).

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
  rebuilds `hbox1` as fx/mask/fill-adjust/group/new/delete. Eye column was already on the left (VISIBILITY_COL → 0).
- Colour: `WGConfig::defaultColorSelectorConfiguration` = Square + Slider (SV + H); commonColors off. Layout colour group
  = WideGamutColorSelector + SpecificColorSelector + PaletteDocker.
- Menus: `krita/gigasketch.xmlgui` (generated from krita5.xmlgui; version `GIGA_XMLGUI_VERSION` in krita/CMakeLists.txt,
  added to krita.qrc). Loaded in KisMainWindow ctor instead of krita5.xmlgui. Keep `<Menu name="tools">` (Python uses
  tools/scripts). Labels renamed in `KisMainWindow::gigaApplyWording()` (action name → text).
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
