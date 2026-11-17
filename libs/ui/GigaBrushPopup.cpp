/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaBrushPopup.h"

#include <QAbstractItemView>
#include <QApplication>
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
#include "widgets/kis_paintop_presets_chooser_popup.h"

namespace {
const char *const kModeKey = "gigaRightClickMode";
const char *const kRecentKey = "gigaRecentBrushes";
const int kMaxRecent = 8;
const char *const kObjectName = "GigaBrushPopup";

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
    m_size = new KisDoubleSliderSpinBox(this);
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
    resize(330, 470);

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

void GigaBrushPopup::popup(const QPoint &position)
{
    syncFromCanvas();
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
