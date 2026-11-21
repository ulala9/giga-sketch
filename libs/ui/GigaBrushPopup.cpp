/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaBrushPopup.h"
#include "GigaBrushSize.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <klocalizedstring.h>

#include <KoCanvasResourceProvider.h>
#include <KoResourceServer.h>
#include <brushengine/kis_paintop_settings.h>

#include "KisResourceServerProvider.h"
#include "KisViewManager.h"
#include "canvas/kis_canvas2.h"
#include "kis_config.h"
#include "kis_image_config.h"
#include "kis_paintop_box.h"
#include "kis_popup_palette.h"
#include "input/kis_input_manager.h"
#include <QCursor>
#include "kis_slider_spin_box.h"
#include "tool/kis_smoothing_options.h"
#include "tool/kis_tool.h"
#include "widgets/kis_paintop_presets_chooser_popup.h"

namespace {
const char *const kModeKey = "gigaRightClickMode";
const char *const kRecentKey = "gigaRecentBrushes";
const int kMaxRecent = 8;
const char *const kObjectName = "GigaBrushPopup";
// Height without the Stabilizer section (the preset list takes the rest).
const int kBaseHeight = 470;
// Krita's MAXIMUM_SMOOTHNESS_DISTANCE (kis_tool_brush.cc).
const qreal kMaxSmoothnessDistance = 1000.0;
// Name of the slot in KisToolBrush that refreshes its Tool Options widgets.
const char *const kSyncSlot = "gigaSyncSmoothingWidgets()";
const char *const kOptionsSlot = "gigaSmoothingOptions()";

/// A row "Label:  [slider]" whose label has a fixed width so the sliders line up.
QWidget *makeRow(QWidget *parent, QLabel *label, QWidget *field)
{
    QWidget *row = new QWidget(parent);
    QHBoxLayout *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    label->setParent(row);
    label->setFixedWidth(118);
    h->addWidget(label);
    h->addWidget(field, 1);
    return row;
}

QStringList &recentList()
{
    static QStringList list;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        list = KisConfig(true).readEntry<QString>(kRecentKey, QString()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    return list;
}

/// The round "auto" brush tips have a Fade setting (our Hardness). Image tips don't.
bool presetHasHardness(KisPaintOpPresetSP preset)
{
    if (!preset || !preset->settings()) return false;
    const QString def = preset->settings()->getString(QStringLiteral("brush_definition"));
    return def.contains(QLatin1String("auto_brush")) && def.contains(QLatin1String("hfade"));
}
} // namespace

GigaBrushPopup::RightClickMode GigaBrushPopup::rightClickMode()
{
    const int mode = KisConfig(true).readEntry<int>(kModeKey, int(BrushPopup));
    return mode == int(PopupPalette) ? PopupPalette : BrushPopup;
}

void GigaBrushPopup::setRightClickMode(RightClickMode mode)
{
    KisConfig(false).writeEntry<int>(kModeKey, int(mode));
}

GigaBrushPopup *GigaBrushPopup::forCanvas(KisCanvas2 *canvas)
{
    if (!canvas || !canvas->canvasWidget()) return nullptr;
    QWidget *canvasWidget = canvas->canvasWidget();
    // The canvas widget is replaced when the canvas changes (e.g. OpenGL on/off);
    // the old popup goes away with the old widget and a new one is made here.
    if (GigaBrushPopup *existing = canvasWidget->findChild<GigaBrushPopup *>(QLatin1String(kObjectName), Qt::FindDirectChildrenOnly)) {
        return existing;
    }
    return new GigaBrushPopup(canvas, canvasWidget);
}

void GigaBrushPopup::noteRecentPreset(KisPaintOpPresetSP preset)
{
    if (!preset) return;
    const QString name = preset->name();
    if (name.isEmpty()) return;
    QStringList &list = recentList();
    if (!list.isEmpty() && list.first() == name) return;
    list.removeAll(name);
    list.prepend(name);
    while (list.size() > kMaxRecent) list.removeLast();
    KisConfig(false).writeEntry<QString>(kRecentKey, list.join(QLatin1Char('\n')));
}

QStringList GigaBrushPopup::recentPresetNames()
{
    return recentList();
}

void GigaBrushPopup::showPopupPalette(KisViewManager *viewManager)
{
    KisCanvas2 *canvas = viewManager ? viewManager->canvasBase() : nullptr;
    KisPopupPalette *palette = canvas ? canvas->popupPalette() : nullptr;
    if (!palette || !canvas->canvasWidget()) return;
    if (palette->onScreen()) {
        palette->dismiss();
        return;
    }
    QWidget *canvasWidget = canvas->canvasWidget();
    QPoint pos = canvasWidget->mapFromGlobal(QCursor::pos());
    if (!canvasWidget->rect().contains(pos)) pos = canvasWidget->rect().center();
    // Same steps as the right-click action (KisPopupWidgetAction): the input
    // manager closes it again on the next click on the canvas.
    if (KisInputManager *input = viewManager->inputManager()) input->registerPopupWidget(palette);
    palette->popup(pos);
}

GigaBrushPopup::GigaBrushPopup(KisCanvas2 *canvas, QWidget *parent)
    : QFrame(parent)
    , m_canvas(canvas)
{
    setObjectName(QLatin1String(kObjectName));
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Window);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::StrongFocus);
    // Mouse/tablet/wheel events that the popup's widgets don't use must not travel
    // on to the canvas widget below (they would paint, pan or close the popup).
    setAttribute(Qt::WA_NoMousePropagation);
    hide();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    QGridLayout *sliders = new QGridLayout();
    sliders->setHorizontalSpacing(8);
    sliders->setVerticalSpacing(6);

    QLabel *sizeLabel = new QLabel(i18nc("brush size", "Size:"), this);
    m_size = new GigaBrushSizeSpinBox(this);
    m_size->setObjectName(QStringLiteral("GigaBrushPopupSize"));
    m_size->setRange(0.01, KisImageConfig(true).maxBrushSize(), 2);
    m_size->setSingleStep(1);
    m_size->setExponentRatio(3.0);
    m_size->setSuffix(i18n(" px"));
    sliders->addWidget(sizeLabel, 0, 0);
    sliders->addWidget(m_size, 0, 1);

    m_hardnessLabel = new QLabel(i18nc("brush tip edge", "Hardness:"), this);
    m_hardness = new KisDoubleSliderSpinBox(this);
    m_hardness->setObjectName(QStringLiteral("GigaBrushPopupHardness"));
    m_hardness->setRange(0, 100, 0);
    m_hardness->setSingleStep(1);
    m_hardness->setSuffix(i18n("%"));
    m_hardness->setToolTip(i18n("How hard the edge of a round brush tip is. Brushes made from a picture have no hardness."));
    sliders->addWidget(m_hardnessLabel, 1, 0);
    sliders->addWidget(m_hardness, 1, 1);
    sliders->setColumnStretch(1, 1);
    layout->addLayout(sliders);

    buildSmoothingSection(layout);

    // Recently used brushes: a row of small thumbnails, newest first.
    m_recentRow = new QWidget(this);
    m_recentLayout = new QHBoxLayout(m_recentRow);
    m_recentLayout->setContentsMargins(0, 0, 0, 0);
    m_recentLayout->setSpacing(3);
    layout->addWidget(m_recentRow);

    m_presets = new KisPaintOpPresetsChooserPopup(this);
    m_presets->setMinimumHeight(300);
    layout->addWidget(m_presets, 1);

    setFixedWidth(330);
    resize(330, kBaseHeight);

    connect(m_size, qOverload<double>(&KisDoubleSliderSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing || !m_canvas) return;
        m_canvas->resourceManager()->setResource(KoCanvasResource::Size, value);
    });
    connect(m_hardness, qOverload<double>(&KisDoubleSliderSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing || !m_canvas) return;
        // Krita's Fade: 1.0 = solid to the edge (hard), lower = softer edge.
        m_canvas->resourceManager()->setResource(KoCanvasResource::Fade, value / 100.0);
    });
    connect(m_presets, &KisPaintOpPresetsChooserPopup::resourceSelected, this, &GigaBrushPopup::slotPresetChosen);
    connect(m_presets, &KisPaintOpPresetsChooserPopup::resourceClicked, this, &GigaBrushPopup::slotPresetChosen);
    if (m_canvas) {
        connect(m_canvas->resourceManager(), &KoCanvasResourceProvider::canvasResourceChanged,
                this, &GigaBrushPopup::slotCanvasResourceChanged);
    }

    // Double-click a brush = pick it and close (single click picks and stays open).
    if (QAbstractItemView *view = m_presets->findChild<QAbstractItemView *>()) {
        view->viewport()->installEventFilter(this);
    }
}

GigaBrushPopup::~GigaBrushPopup()
{
    if (m_watchingApp) qApp->removeEventFilter(this);
}

void GigaBrushPopup::setTool(KisTool *tool)
{
    m_tool = tool;
}

void GigaBrushPopup::popup(const QPoint &position)
{
    syncFromCanvas();
    syncSmoothing();
    rebuildRecentRow();
    show();
    raise();
    placeAt(position);
    setFocus(Qt::PopupFocusReason);
    if (!m_watchingApp) {
        // Closes the popup on a click in another part of the window (panels, top
        // bar). Clicks on the canvas are handled by the input manager instead,
        // which also eats that click so it doesn't paint.
        qApp->installEventFilter(this);
        m_watchingApp = true;
    }
}

void GigaBrushPopup::dismiss()
{
    hide();
}

bool GigaBrushPopup::onScreen()
{
    return isVisible();
}

void GigaBrushPopup::placeAt(const QPoint &position)
{
    QWidget *parent = parentWidget();
    if (!parent) return;
    // Like the popup palette, keep it inside the canvas; open with its top-left
    // corner a little right/below the pointer so the brush cursor stays visible.
    QRect r(position + QPoint(8, 8), size());
    const QRect fit = parent->rect().adjusted(4, 4, -4, -4);
    if (r.right() > fit.right()) r.moveRight(position.x() - 8);
    if (r.bottom() > fit.bottom()) r.moveBottom(fit.bottom());
    if (r.left() < fit.left()) r.moveLeft(fit.left());
    if (r.top() < fit.top()) r.moveTop(fit.top());
    move(r.topLeft());
}

void GigaBrushPopup::syncFromCanvas()
{
    if (!m_canvas) return;
    KoCanvasResourceProvider *resources = m_canvas->resourceManager();
    KisPaintOpPresetSP preset = resources->resource(KoCanvasResource::CurrentPaintOpPreset).value<KisPaintOpPresetSP>();

    m_syncing = true;
    m_size->setValue(resources->resource(KoCanvasResource::Size).toDouble());
    const bool hasHardness = presetHasHardness(preset);
    m_hardness->setEnabled(hasHardness);
    m_hardnessLabel->setEnabled(hasHardness);
    if (hasHardness) {
        m_hardness->setValue(qBound(0.0, resources->resource(KoCanvasResource::Fade).toDouble(), 1.0) * 100.0);
    }
    if (preset) m_presets->canvasResourceChanged(preset);
    m_syncing = false;
}

void GigaBrushPopup::rebuildRecentRow()
{
    while (QLayoutItem *item = m_recentLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    KisPaintOpPresetResourceServer *server = KisResourceServerProvider::instance()->paintOpPresetServer();
    KisPaintOpPresetSP current = m_canvas
        ? m_canvas->resourceManager()->resource(KoCanvasResource::CurrentPaintOpPreset).value<KisPaintOpPresetSP>()
        : KisPaintOpPresetSP();
    int shown = 0;
    for (const QString &name : recentList()) {
        KisPaintOpPresetSP preset = server ? server->resource(QString(), QString(), name) : KisPaintOpPresetSP();
        if (!preset) continue;
        QToolButton *button = new QToolButton(m_recentRow);
        button->setAutoRaise(true);
        button->setCheckable(true);
        button->setChecked(current && current->name() == name);
        button->setIconSize(QSize(30, 30));
        button->setIcon(QIcon(QPixmap::fromImage(preset->image())));
        button->setToolTip(name);
        button->setProperty("gigaPresetName", name);
        connect(button, &QToolButton::clicked, this, [this, preset]() { choosePreset(preset); });
        m_recentLayout->addWidget(button);
        ++shown;
    }
    m_recentLayout->addStretch(1);
    m_recentRow->setVisible(shown > 0);
}

void GigaBrushPopup::choosePreset(KoResourceSP resource)
{
    if (!resource || !m_canvas || !m_canvas->viewManager()) return;
    if (KisPaintopBox *box = m_canvas->viewManager()->paintOpBox()) {
        box->resourceSelected(resource);
    }
    // Size/hardness of the new brush; the recent row updates on next open.
    syncFromCanvas();
}

void GigaBrushPopup::slotPresetChosen(KoResourceSP resource)
{
    if (m_syncing) return;
    choosePreset(resource);
}

void GigaBrushPopup::slotCanvasResourceChanged(int key, const QVariant &)
{
    if (!isVisible() || m_syncing) return;
    // e.g. [ and ] while the popup is open, or a brush picked elsewhere.
    if (key == KoCanvasResource::Size || key == KoCanvasResource::Fade || key == KoCanvasResource::CurrentPaintOpPreset) {
        syncFromCanvas();
    }
}

bool GigaBrushPopup::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        // Esc/Enter close the popup instead of triggering window shortcuts.
        QKeyEvent *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            event->accept();
            return true;
        }
    }
    return QFrame::event(event);
}

void GigaBrushPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        dismiss();
        Q_EMIT finished();
    }
    // Keep every other key here too: a key reaching the canvas widget could start
    // a canvas action (Space = pan, …) while the popup is open.
    event->accept();
}

void GigaBrushPopup::mousePressEvent(QMouseEvent *event)
{
    event->accept();
}

void GigaBrushPopup::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
}

void GigaBrushPopup::wheelEvent(QWheelEvent *event)
{
    event->accept();
}

void GigaBrushPopup::hideEvent(QHideEvent *event)
{
    if (m_watchingApp) {
        qApp->removeEventFilter(this);
        m_watchingApp = false;
    }
    QWidget *focus = QApplication::focusWidget();
    if (focus && (focus == this || isAncestorOf(focus)) && parentWidget()) {
        parentWidget()->setFocus(Qt::OtherFocusReason); // back to the canvas
    }
    QFrame::hideEvent(event);
}

bool GigaBrushPopup::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();

    if (type == QEvent::MouseButtonDblClick && watched->parent() && m_presets->isAncestorOf(qobject_cast<QWidget *>(watched))) {
        // The first click already picked the brush; close once that is done.
        QTimer::singleShot(0, this, [this]() {
            if (!isVisible()) return;
            dismiss();
            Q_EMIT finished();
        });
        return false;
    }

    if (m_watchingApp && (type == QEvent::MouseButtonPress || type == QEvent::TabletPress)) {
        QWidget *w = qobject_cast<QWidget *>(watched);
        if (!w || !isVisible()) return false;
        if (w == this || isAncestorOf(w)) return false;
        // Menus and lists opened from inside the popup (tags, view mode) are
        // their own windows; so are other app windows. Leave those alone.
        if (w->window() != window()) return false;
        QWidget *canvasWidget = parentWidget();
        if (canvasWidget && (w == canvasWidget || canvasWidget->isAncestorOf(w))) return false;
        dismiss();
        Q_EMIT finished();
    }
    return false;
}

// ---------------------------------------------------------------------------
// Stabilizer section
//
// The popup edits the active tool's KisSmoothingOptions directly: that is the
// same object the Tool Options sliders edit, and it saves itself to the config.
// After each change, the tool's Tool Options widgets are refreshed through the
// gigaSyncSmoothingWidgets() slot. All of this happens on the GUI thread while
// no stroke runs (the popup closes before the next press reaches the canvas),
// so the paint thread never sees a half-changed setting.
// ---------------------------------------------------------------------------

void GigaBrushPopup::buildSmoothingSection(QLayout *layout)
{
    m_smoothingSection = new QWidget(this);
    m_smoothingSection->setObjectName(QStringLiteral("GigaBrushPopupStabilizer"));
    QVBoxLayout *v = new QVBoxLayout(m_smoothingSection);
    v->setContentsMargins(0, 2, 0, 0);
    v->setSpacing(6);

    QWidget *section = m_smoothingSection;

    // Mode dropdown, same items and order as Tool Options (index == enum value).
    m_smoothingMode = new QComboBox(section);
    m_smoothingMode->setObjectName(QStringLiteral("GigaBrushPopupSmoothingMode"));
    m_smoothingMode->addItems(QStringList()
                              << i18nc("@item:inlistbox Brush Smoothing", "None")
                              << i18nc("@item:inlistbox Brush Smoothing", "Basic")
                              << i18nc("@item:inlistbox Brush Smoothing", "Weighted")
                              << i18nc("@item:inlistbox Brush Smoothing", "Stabilizer")
                              << i18nc("@item:inlistbox Brush Smoothing", "Pixel")
                              << i18nc("@item:inlistbox Brush Smoothing", "Smooth Curve"));
    m_smoothingMode->setToolTip(i18n("How the brush smooths your stroke. The same setting as Brush Smoothing in Tool Options."));
    v->addWidget(makeRow(section, new QLabel(i18n("Stabilizer:")), m_smoothingMode));

    auto percentSlider = [section](const QString &tip) {
        KisSliderSpinBox *slider = new KisSliderSpinBox(section);
        slider->setRange(0, 100);
        slider->setSuffix(i18n("%"));
        slider->setToolTip(tip);
        return slider;
    };

    // Smooth Curve
    m_curveAmount = percentSlider(i18nc("@info:tooltip", "How strongly the stroke path is smoothed. Higher values give smoother lines that trail further behind the pen."));
    m_curveAmount->setObjectName(QStringLiteral("GigaBrushPopupCurveAmount"));
    m_curveAmountRow = makeRow(section, new QLabel(i18n("Amount:")), m_curveAmount);
    v->addWidget(m_curveAmountRow);

    m_curveSpeed = percentSlider(i18nc("@info:tooltip", "How much drawing faster reduces smoothing, so quick flicks stay crisp while slow curves are smoothed."));
    m_curveSpeedRow = makeRow(section, new QLabel(i18n("Speed Influence:")), m_curveSpeed);
    v->addWidget(m_curveSpeedRow);

    m_curvePressure = percentSlider(i18nc("@info:tooltip", "How much a light touch adds smoothing."));
    m_curvePressureRow = makeRow(section, new QLabel(i18n("Pressure Influence:")), m_curvePressure);
    v->addWidget(m_curvePressureRow);

    m_curveCatchUp = new KisSliderSpinBox(section);
    m_curveCatchUp->setRange(0, 400);
    m_curveCatchUp->setSuffix(i18n(" ms"));
    m_curveCatchUp->setToolTip(i18nc("@info:tooltip", "How long the line takes to glide into place after the pen lifts. 0 lands instantly. Off when Catch Up is off in Tool Options."));
    m_curveCatchUpRow = makeRow(section, new QLabel(i18n("Catch-Up Time:")), m_curveCatchUp);
    v->addWidget(m_curveCatchUpRow);

    // Weighted + Stabilizer: the distance (or sample count) at slow and fast pen
    // speed. Krita calls them "at min speed" (= smoothnessDistanceMax) and
    // "at max speed" (= smoothnessDistanceMin).
    m_distanceSlow = new KisDoubleSliderSpinBox(section);
    m_distanceSlow->setObjectName(QStringLiteral("GigaBrushPopupDistanceSlow"));
    m_distanceSlow->setExponentRatio(3.0);
    m_distanceSlowLabel = new QLabel();
    m_distanceSlowRow = makeRow(section, m_distanceSlowLabel, m_distanceSlow);
    v->addWidget(m_distanceSlowRow);

    m_distanceFast = new KisDoubleSliderSpinBox(section);
    m_distanceFast->setExponentRatio(3.0);
    m_distanceFastLabel = new QLabel();
    m_distanceFastRow = makeRow(section, m_distanceFastLabel, m_distanceFast);
    v->addWidget(m_distanceFastRow);

    // Stabilizer: delay (a dead zone around the pen where the line waits).
    {
        QWidget *field = new QWidget(section);
        QHBoxLayout *h = new QHBoxLayout(field);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        m_delayOn = new QCheckBox(field);
        m_delayOn->setToolTip(i18n("Delay the brush stroke to make the line smoother"));
        m_delay = new KisDoubleSliderSpinBox(field);
        m_delay->setRange(0, 500);
        m_delay->setExponentRatio(3.0);
        m_delay->setSuffix(i18n(" px"));
        m_delay->setToolTip(i18n("Radius where the brush is blocked"));
        h->addWidget(m_delayOn);
        h->addWidget(m_delay, 1);
        m_delayRow = makeRow(section, new QLabel(i18n("Delay:")), field);
        v->addWidget(m_delayRow);
    }

    // Weighted: stroke ending.
    m_tail = new KisDoubleSliderSpinBox(section);
    m_tail->setRange(0.0, 1.0, 2);
    m_tail->setSingleStep(0.01);
    m_tail->setToolTip(i18n("How much the end of the stroke is smoothed"));
    m_tailRow = makeRow(section, new QLabel(i18n("Stroke Ending:")), m_tail);
    v->addWidget(m_tailRow);

    layout->addWidget(m_smoothingSection);

    // --- changes from the popup go to the tool's smoothing options ---
    connect(m_smoothingMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_syncing) return;
        KisSmoothingOptions *o = smoothing();
        if (!o || index < 0) return;
        o->setSmoothingType(KisSmoothingOptions::SmoothingType(index));
        pushSmoothingToTool();
        syncSmoothing(); // shows this mode's rows
    });
    auto onPercent = [this](KisSliderSpinBox *slider, void (KisSmoothingOptions::*setter)(qreal)) {
        connect(slider, qOverload<int>(&KisSliderSpinBox::valueChanged), this, [this, setter](int percent) {
            if (m_syncing) return;
            if (KisSmoothingOptions *o = smoothing()) {
                (o->*setter)(percent / 100.0);
                pushSmoothingToTool();
            }
        });
    };
    onPercent(m_curveAmount, &KisSmoothingOptions::setCurveAmount);
    onPercent(m_curveSpeed, &KisSmoothingOptions::setCurveSpeedInfluence);
    onPercent(m_curvePressure, &KisSmoothingOptions::setCurvePressureInfluence);
    connect(m_curveCatchUp, qOverload<int>(&KisSliderSpinBox::valueChanged), this, [this](int ms) {
        if (m_syncing) return;
        if (KisSmoothingOptions *o = smoothing()) {
            o->setCurveCatchUpTime(ms);
            pushSmoothingToTool();
        }
    });
    connect(m_distanceSlow, qOverload<double>(&KisDoubleSliderSpinBox::valueChanged), this, [this](double value) {
        if (!m_syncing) setDistance(true, value);
    });
    connect(m_distanceFast, qOverload<double>(&KisDoubleSliderSpinBox::valueChanged), this, [this](double value) {
        if (!m_syncing) setDistance(false, value);
    });
    connect(m_delayOn, &QCheckBox::toggled, this, [this](bool on) {
        m_delay->setEnabled(on);
        if (m_syncing) return;
        if (KisSmoothingOptions *o = smoothing()) {
            o->setUseDelayDistance(on);
            pushSmoothingToTool();
        }
    });
    connect(m_delay, qOverload<double>(&KisDoubleSliderSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) return;
        if (KisSmoothingOptions *o = smoothing()) {
            o->setDelayDistance(value);
            pushSmoothingToTool();
        }
    });
    connect(m_tail, qOverload<double>(&KisDoubleSliderSpinBox::valueChanged), this, [this](double value) {
        if (m_syncing) return;
        if (KisSmoothingOptions *o = smoothing()) {
            o->setTailAggressiveness(value);
            pushSmoothingToTool();
        }
    });
}

KisSmoothingOptions *GigaBrushPopup::smoothing() const
{
    // Only tools with the smoothing UI in Tool Options (Freehand Brush and
    // Multibrush, both KisToolBrush). Fill, gradient, shapes, Dynamic Brush, …
    // don't have these slots, so they get no section.
    KisTool *tool = m_tool.data();
    if (!tool) return nullptr;
    const QMetaObject *meta = tool->metaObject();
    if (meta->indexOfSlot(kSyncSlot) < 0 || meta->indexOfSlot(kOptionsSlot) < 0) return nullptr;
    QObject *options = nullptr;
    QMetaObject::invokeMethod(tool, "gigaSmoothingOptions", Qt::DirectConnection, Q_RETURN_ARG(QObject *, options));
    return qobject_cast<KisSmoothingOptions *>(options);
}

void GigaBrushPopup::syncSmoothing()
{
    KisSmoothingOptions *o = smoothing();
    m_smoothingSection->setVisible(o != nullptr);
    if (o) {
        m_syncing = true;
        m_smoothingMode->setCurrentIndex(int(o->smoothingType()));
        m_curveAmount->setValue(qRound(o->curveAmount() * 100));
        m_curveSpeed->setValue(qRound(o->curveSpeedInfluence() * 100));
        m_curvePressure->setValue(qRound(o->curvePressureInfluence() * 100));
        m_curveCatchUp->setValue(o->curveCatchUpTime());
        m_curveCatchUp->setEnabled(o->curveCatchUp());
        updateSmoothingRows(); // ranges first, then the distance values
        m_distanceSlow->setValue(o->smoothnessDistanceMax());
        m_distanceFast->setValue(o->smoothnessDistanceMin());
        m_delayOn->setChecked(o->useDelayDistance());
        m_delay->setEnabled(o->useDelayDistance());
        m_delay->setValue(o->delayDistance());
        m_tail->setValue(o->tailAggressiveness());
        m_syncing = false;
    }
    fitHeight();
}

void GigaBrushPopup::updateSmoothingRows()
{
    const int type = m_smoothingMode->currentIndex();
    const bool curve = type == KisSmoothingOptions::CURVE_SMOOTHING;
    const bool weighted = type == KisSmoothingOptions::WEIGHTED_SMOOTHING;
    const bool stabilizer = type == KisSmoothingOptions::STABILIZER;

    m_curveAmountRow->setVisible(curve);
    m_curveSpeedRow->setVisible(curve);
    m_curvePressureRow->setVisible(curve);
    m_curveCatchUpRow->setVisible(curve);
    m_distanceSlowRow->setVisible(weighted || stabilizer);
    m_distanceFastRow->setVisible(weighted || stabilizer);
    m_delayRow->setVisible(stabilizer);
    m_tailRow->setVisible(weighted);

    // Same ranges/wording as Tool Options: the stabilizer counts samples (whole
    // numbers), weighted smoothing measures a distance in pixels.
    const bool wasSyncing = m_syncing;
    m_syncing = true;
    if (stabilizer) {
        m_distanceSlowLabel->setText(i18n("Samples (slow):"));
        m_distanceFastLabel->setText(i18n("Samples (fast):"));
        m_distanceSlow->setRange(3.0, kMaxSmoothnessDistance, 0);
        m_distanceFast->setRange(3.0, kMaxSmoothnessDistance, 0);
        m_distanceSlow->setSuffix(QString());
        m_distanceFast->setSuffix(QString());
        m_distanceSlow->setToolTip(i18n("How many pen positions are averaged when you draw slowly. More = steadier, more lag."));
        m_distanceFast->setToolTip(i18n("How many pen positions are averaged when you draw fast."));
    } else {
        m_distanceSlowLabel->setText(i18n("Distance (slow):"));
        m_distanceFastLabel->setText(i18n("Distance (fast):"));
        m_distanceSlow->setRange(3.0, kMaxSmoothnessDistance, 1);
        m_distanceFast->setRange(3.0, kMaxSmoothnessDistance, 1);
        m_distanceSlow->setSuffix(i18n(" px"));
        m_distanceFast->setSuffix(i18n(" px"));
        m_distanceSlow->setToolTip(i18n("How far the line is smoothed when you draw slowly."));
        m_distanceFast->setToolTip(i18n("How far the line is smoothed when you draw fast."));
    }
    m_distanceSlow->setSingleStep(1);
    m_distanceFast->setSingleStep(1);
    m_syncing = wasSyncing;
}

void GigaBrushPopup::setDistance(bool slow, qreal value)
{
    KisSmoothingOptions *o = smoothing();
    if (!o) return;
    const qreal oldSlow = o->smoothnessDistanceMax();
    const qreal oldFast = o->smoothnessDistanceMin();

    qreal newSlow = slow ? value : oldSlow;
    qreal newFast = slow ? oldFast : value;
    // The chain button in Tool Options locks the two together: keep their ratio
    // here too, or Tool Options would undo it on the next refresh.
    if (o->smoothnessDistanceKeepAspectRatio() && oldSlow > 0 && oldFast > 0) {
        if (slow) newFast = qBound(3.0, value * oldFast / oldSlow, kMaxSmoothnessDistance);
        else newSlow = qBound(3.0, value * oldSlow / oldFast, kMaxSmoothnessDistance);
    }
    o->setSmoothnessDistanceMax(newSlow);
    o->setSmoothnessDistanceMin(newFast);

    m_syncing = true;
    if (slow) m_distanceFast->setValue(newFast);
    else m_distanceSlow->setValue(newSlow);
    m_syncing = false;

    pushSmoothingToTool();
}

void GigaBrushPopup::pushSmoothingToTool()
{
    if (smoothing()) {
        QMetaObject::invokeMethod(m_tool.data(), "gigaSyncSmoothingWidgets", Qt::DirectConnection);
    }
}

void GigaBrushPopup::fitHeight()
{
    int h = kBaseHeight;
    if (m_smoothingSection->isVisibleTo(this)) {
        h += m_smoothingSection->sizeHint().height() + (layout() ? layout()->spacing() : 0);
    }
    if (h == height()) return;
    const QPoint topLeft = pos();
    resize(width(), h);
    if (isVisible() && parentWidget()) {
        // Grew past the bottom of the canvas (mode switch): move it up.
        const QRect fit = parentWidget()->rect().adjusted(4, 4, -4, -4);
        QRect r(topLeft, size());
        if (r.bottom() > fit.bottom()) r.moveBottom(fit.bottom());
        if (r.top() < fit.top()) r.moveTop(fit.top());
        move(r.topLeft());
    }
}
