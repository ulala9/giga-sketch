/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "giga_tool_stroke_points.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QWheelEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QToolButton>

#include <KoPointerEvent.h>
#include <KoCanvasBase.h>
#include <kis_cursor.h>
#include <kis_slider_spin_box.h>
#include <kis_config.h>

#include <kis_node.h>
#include <kis_image.h>
#include <kis_paint_device.h>
#include <kis_selection.h>
#include "GigaLastStrokeTransform.h"

namespace {
const qreal DotRadius = 4.5;      // screen px
const qreal DotHoverRadius = 6.0; // screen px
const qreal HitRadius = 9.0;      // screen px
const qreal LineHitRadius = 10.0; // screen px, double-click on the line
const char *PropKey = "gigaStrokePointsProportional";
const char *RadiusKey = "gigaStrokePointsRadius";      // screen px
const char *FalloffKey = "gigaStrokePointsFalloff";    // GigaStrokePath::Falloff
const int MinRadius = 5;
const int MaxRadius = 3000;
}

GigaToolStrokePoints::GigaToolStrokePoints(KoCanvasBase *canvas)
    : KisTool(canvas, KisCursor::arrowCursor())
{
    setObjectName(QStringLiteral("tool_giga_stroke_points"));
    KisConfig cfg(true);
    m_proportional = cfg.readEntry<bool>(PropKey, false);
    m_radius = qBound(MinRadius, cfg.readEntry<int>(RadiusKey, 120), MaxRadius);
    m_falloff = qBound(0, cfg.readEntry<int>(FalloffKey, 0), int(GigaStrokePath::Constant));
}

GigaToolStrokePoints::~GigaToolStrokePoints()
{
}

GigaLastStrokeTransform *GigaToolStrokePoints::session() const
{
    return GigaLastStrokeTransform::pointsSession();
}

void GigaToolStrokePoints::connectSession()
{
    GigaLastStrokeTransform *s = session();
    if (!s || m_session == s) return;
    m_session = s;
    connect(s, SIGNAL(sigPointsChanged()), this, SLOT(slotSessionChanged()), Qt::UniqueConnection);
    connect(s, SIGNAL(sigPointsEnded()), this, SLOT(slotSessionEnded()), Qt::UniqueConnection);
}

void GigaToolStrokePoints::activate(const QSet<KoShape*> &shapes)
{
    KisTool::activate(shapes);
    m_selected.clear();
    m_hover = -1;
    m_dragging = false;
    m_moved = false;
    m_session.clear();
    connectSession();

    GigaLastStrokeTransform *s = session();
    if (s && s->path().vertices().size() <= 1 && !s->path().isEdited()) {
        // First opening: dots about "Point spacing" screen px apart.
        s->path().placeVertices(GigaLastStrokeTransform::pointSpacing() / viewScale());
    }
    if (m_pointsButton) m_pointsButton->setChecked(true);
    updateOverlay();
}

void GigaToolStrokePoints::deactivate()
{
    // Switching to another tool applies the edit.
    if (GigaLastStrokeTransform *s = session()) {
        s->pointsFinish(true, false);
    }
    setWheelCatcher(false);
    updateOverlay();
    m_selected.clear();
    m_dragging = false;
    m_propDrag = false;
    KisTool::deactivate();
}

qreal GigaToolStrokePoints::viewScale() const
{
    const QPointF o = pixelToView(QPointF(0, 0));
    const qreal sx = QLineF(o, pixelToView(QPointF(100, 0))).length() / 100.0;
    const qreal sy = QLineF(o, pixelToView(QPointF(0, 100))).length() / 100.0;
    const qreal scale = 0.5 * (sx + sy);
    return scale > 1e-6 ? scale : 1.0;
}

int GigaToolStrokePoints::hitVertex(const QPointF &imagePos) const
{
    GigaLastStrokeTransform *s = session();
    if (!s) return -1;
    const QPointF at = pixelToView(imagePos);
    int best = -1;
    qreal bestDistance = HitRadius;
    for (int i = 0; i < s->path().vertices().size(); ++i) {
        const qreal d = QLineF(at, pixelToView(s->path().vertexPosition(i))).length();
        if (d <= bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

void GigaToolStrokePoints::press(KoPointerEvent *event, bool toggleSelection)
{
    GigaLastStrokeTransform *s = session();
    m_dragging = false;
    m_moved = false;
    if (!s) return;

    const QPointF pos = convertToPixelCoord(event);
    const int hit = hitVertex(pos);
    if (hit < 0) {
        if (!toggleSelection) m_selected.clear();
        updateOverlay();
        return;
    }
    if (toggleSelection) {
        if (m_selected.contains(hit)) {
            m_selected.remove(hit);
            updateOverlay();
            return;
        }
        m_selected.insert(hit);
    } else if (!m_selected.contains(hit)) {
        m_selected = {hit};
    }

    m_dragging = true;
    m_dragStart = pos;
    m_startOffsets.clear();
    for (const GigaStrokePath::Vertex &v : s->path().vertices()) m_startOffsets << v.offset;
    m_propDrag = m_proportional;
    if (m_propDrag) setWheelCatcher(true); // wheel = radius while dragging
    updateOverlay();
}

void GigaToolStrokePoints::drag(KoPointerEvent *event)
{
    GigaLastStrokeTransform *s = session();
    if (!m_dragging || !s) return;
    const QPointF delta = convertToPixelCoord(event) - m_dragStart;
    if (!m_moved) {
        // A click without moving changes nothing (and adds no undo step).
        if (QLineF(QPointF(), delta).length() * viewScale() < 2.0) return;
        s->pointsBeginEdit();
        m_moved = true;
        if (m_propDrag) {
            // One proportional move for this drag; the dots' own offsets
            // stay, the move pulls the line around them.
            GigaStrokePath::Move move;
            for (int i : m_selected) {
                if (i >= 0 && i < s->path().vertices().size()) move.centers << s->path().vertices()[i].s;
            }
            move.radius = m_radius / viewScale();
            move.falloff = m_falloff;
            s->path().addMove(move);
        }
    }
    if (m_propDrag) {
        if (GigaStrokePath::Move *move = s->path().lastMove()) move->delta = delta;
    } else {
        for (int i : m_selected) {
            if (i >= 0 && i < m_startOffsets.size()) s->path().setOffset(i, m_startOffsets[i] + delta);
        }
    }
    // Repaint live while dragging. The controller runs one repaint at a
    // time and folds the moves made meanwhile into the next one, so this
    // never queues up work or waits on the painting threads.
    s->pointsChanged();
    updateOverlay();
}

void GigaToolStrokePoints::release()
{
    GigaLastStrokeTransform *s = session();
    const bool moved = m_dragging && m_moved;
    m_dragging = false;
    m_moved = false;
    m_propDrag = false;
    setWheelCatcher(false);
    if (s && moved) s->pointsChanged(); // final shape (the live repaints may lag one step)
    updateOverlay();
}

void GigaToolStrokePoints::beginPrimaryAction(KoPointerEvent *event)
{
    press(event, false);
}

void GigaToolStrokePoints::continuePrimaryAction(KoPointerEvent *event)
{
    drag(event);
}

void GigaToolStrokePoints::endPrimaryAction(KoPointerEvent *event)
{
    Q_UNUSED(event);
    release();
}

void GigaToolStrokePoints::beginPrimaryDoubleClickAction(KoPointerEvent *event)
{
    GigaLastStrokeTransform *s = session();
    const QPointF pos = convertToPixelCoord(event);
    if (!s || hitVertex(pos) >= 0) {
        press(event, false);
        return;
    }
    qreal distance = 0.0;
    const qreal at = s->path().nearestS(pos, &distance);
    if (distance * viewScale() > LineHitRadius) {
        press(event, false);
        return;
    }
    // A new dot on the line (the shape doesn't change; Ctrl+Z removes it).
    s->pointsBeginEdit();
    const int index = s->path().insertVertex(at);
    m_selected = {index};
    m_dragging = false;
    updateOverlay();
}

void GigaToolStrokePoints::beginAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    // Shift+click (Krita's "change brush size" gesture) = select more dots.
    if (action == ChangeSize || action == ChangeSizeSnap) {
        press(event, true);
        return;
    }
    KisTool::beginAlternateAction(event, action);
}

void GigaToolStrokePoints::continueAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    if (action == ChangeSize || action == ChangeSizeSnap) {
        drag(event);
        return;
    }
    KisTool::continueAlternateAction(event, action);
}

void GigaToolStrokePoints::endAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    if (action == ChangeSize || action == ChangeSizeSnap) {
        release();
        return;
    }
    KisTool::endAlternateAction(event, action);
}

void GigaToolStrokePoints::mouseMoveEvent(KoPointerEvent *event)
{
    KisTool::mouseMoveEvent(event);
    if (m_dragging) return;
    const int hover = hitVertex(convertToPixelCoord(event));
    if (hover != m_hover) {
        m_hover = hover;
        useCursor(hover >= 0 ? QCursor(Qt::SizeAllCursor) : KisCursor::arrowCursor());
        updateOverlay();
    }
}

void GigaToolStrokePoints::requestStrokeEnd()
{
    GigaLastStrokeTransform *s = session();
    // Not when the request comes from the controller starting its repaint.
    if (s && !s->startingRepaint()) s->pointsFinish(true, true);
}

void GigaToolStrokePoints::requestStrokeCancellation()
{
    if (GigaLastStrokeTransform *s = session()) s->pointsFinish(false, true);
}

void GigaToolStrokePoints::updateOverlay()
{
    QRectF rect = m_lastOverlay;
    if (GigaLastStrokeTransform *s = session()) {
        const qreal margin = 12.0 / viewScale();
        const QRectF now = s->path().bentBounds().adjusted(-margin, -margin, margin, margin);
        rect = rect.isEmpty() ? now : rect.united(now);
        m_lastOverlay = now;
    } else {
        m_lastOverlay = QRectF();
    }
    if (!rect.isEmpty()) updateCanvasPixelRect(rect);
}

void GigaToolStrokePoints::paint(QPainter &gc, const KoViewConverter &converter)
{
    Q_UNUSED(converter);
    GigaLastStrokeTransform *s = session();
    if (!s) return;
    const GigaStrokePath &path = s->path();

    gc.save();
    gc.setRenderHint(QPainter::Antialiasing, true);

    // While dragging (and until the repaint lands): a faint guide of the new
    // path. The stroke itself repaints live; this only shows where it is
    // heading when a big stroke's repaint is a step behind.
    if ((m_dragging && m_moved) || s->pointsRepainting()) {
        QPolygonF line;
        const QVector<QPointF> points = path.bentPolyline();
        line.reserve(points.size());
        for (const QPointF &p : points) line << pixelToView(p);
        gc.setBrush(Qt::NoBrush);
        gc.setPen(QPen(QColor(0, 0, 0, 45), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        gc.drawPolyline(line);
        gc.setPen(QPen(QColor(255, 255, 255, 90), 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        gc.drawPolyline(line);
    }

    const QColor accent = qApp->palette().color(QPalette::Highlight);

    // Proportional: tint the part of the line that will follow, strong at the
    // dot, fading to nothing at the radius.
    GigaStrokePath::Move reach;
    reach.centers = proportionalCenters();
    reach.radius = m_radius / viewScale();
    reach.falloff = m_falloff;
    const bool showReach = !reach.centers.isEmpty();
    if (showReach) {
        const QVector<QPointF> points = path.bentPolyline();
        const QVector<qreal> &lengths = path.pathLengths();
        for (int i = 0; i + 1 < points.size() && i + 1 < lengths.size(); ++i) {
            const qreal w = GigaStrokePath::moveWeight(reach, 0.5 * (lengths[i] + lengths[i + 1]));
            if (w <= 0.0) continue;
            QColor tint = accent;
            tint.setAlphaF(0.15 + 0.6 * w);
            gc.setPen(QPen(tint, 6.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            gc.drawLine(pixelToView(points[i]), pixelToView(points[i + 1]));
        }
    }

    for (int i = 0; i < path.vertices().size(); ++i) {
        const QPointF c = pixelToView(path.vertexPosition(i));
        const qreal r = (i == m_hover) ? DotHoverRadius : DotRadius;
        QColor fill(255, 255, 255);
        if (m_selected.contains(i)) {
            fill = accent;
        } else if (showReach) {
            // How strongly this dot follows: white -> accent.
            const qreal w = GigaStrokePath::moveWeight(reach, path.vertices()[i].s);
            fill = QColor::fromRgbF(1.0 + (accent.redF() - 1.0) * w * 0.7,
                                    1.0 + (accent.greenF() - 1.0) * w * 0.7,
                                    1.0 + (accent.blueF() - 1.0) * w * 0.7);
        }
        gc.setPen(QPen(QColor(0, 0, 0, 170), 1.5));
        gc.setBrush(fill);
        gc.drawEllipse(c, r, r);
    }
    gc.restore();
}

QWidget *GigaToolStrokePoints::createOptionWidget()
{
    QWidget *w = new QWidget();
    w->setObjectName(QStringLiteral("GigaStrokePointsOptions"));
    QHBoxLayout *layout = new QHBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);

    // Pixels | Points (the same switch as pressing the shortcut again).
    QButtonGroup *modes = new QButtonGroup(w);
    m_pixelsButton = new QToolButton(w);
    m_pixelsButton->setText(i18n("Pixels"));
    m_pixelsButton->setToolTip(i18n("Warp or transform the stroke's pixels (Transform tool)"));
    m_pixelsButton->setCheckable(true);
    m_pointsButton = new QToolButton(w);
    m_pointsButton->setText(i18n("Points"));
    m_pointsButton->setToolTip(i18n("Bend the stroke by its points; the brush repaints it"));
    m_pointsButton->setCheckable(true);
    m_pointsButton->setChecked(true);
    modes->addButton(m_pixelsButton);
    modes->addButton(m_pointsButton);
    modes->setExclusive(true);
    connect(m_pixelsButton, SIGNAL(clicked()), this, SLOT(slotModePixels()));
    layout->addWidget(m_pixelsButton);
    layout->addWidget(m_pointsButton);
    layout->addSpacing(12);

    m_spacing = new KisSliderSpinBox(w);
    m_spacing->setRange(10, 200);
    m_spacing->setPrefix(i18n("Point spacing: "));
    m_spacing->setSuffix(i18n(" px"));
    m_spacing->setValue(GigaLastStrokeTransform::pointSpacing());
    m_spacing->setToolTip(i18n("Distance between the dots on screen. Curvy parts get more dots."));
    m_spacing->setMinimumWidth(170);
    connect(m_spacing, SIGNAL(valueChanged(int)), this, SLOT(slotSpacingChanged(int)));
    layout->addWidget(m_spacing);
    layout->addSpacing(12);

    // Proportional editing: on/off, radius (mouse wheel while dragging), falloff.
    m_propButton = new QToolButton(w);
    m_propButton->setText(i18n("Proportional"));
    m_propButton->setToolTip(i18n("Dragging a dot also pulls the line around it, within the radius "
                                  "(measured along the line). Mouse wheel while dragging: radius."));
    m_propButton->setCheckable(true);
    m_propButton->setChecked(m_proportional);
    connect(m_propButton, SIGNAL(toggled(bool)), this, SLOT(slotProportionalToggled(bool)));
    layout->addWidget(m_propButton);

    m_radiusBox = new KisSliderSpinBox(w);
    m_radiusBox->setRange(MinRadius, MaxRadius);
    m_radiusBox->setExponentRatio(3.0); // fine control for small radii
    m_radiusBox->setPrefix(i18n("Radius: "));
    m_radiusBox->setSuffix(i18n(" px"));
    m_radiusBox->setValue(m_radius);
    m_radiusBox->setToolTip(i18n("How far along the line a drag reaches (on screen). Mouse wheel while dragging."));
    m_radiusBox->setMinimumWidth(150);
    m_radiusBox->setEnabled(m_proportional);
    connect(m_radiusBox, SIGNAL(valueChanged(int)), this, SLOT(slotRadiusChanged(int)));
    layout->addWidget(m_radiusBox);

    m_falloffBox = new QComboBox(w);
    m_falloffBox->addItem(i18n("Smooth"));
    m_falloffBox->addItem(i18n("Sphere"));
    m_falloffBox->addItem(i18n("Root"));
    m_falloffBox->addItem(i18n("Sharp"));
    m_falloffBox->addItem(i18n("Linear"));
    m_falloffBox->addItem(i18n("Constant"));
    m_falloffBox->setCurrentIndex(m_falloff);
    m_falloffBox->setToolTip(i18n("How the pull fades towards the edge of the radius"));
    m_falloffBox->setEnabled(m_proportional);
    connect(m_falloffBox, SIGNAL(currentIndexChanged(int)), this, SLOT(slotFalloffChanged(int)));
    layout->addWidget(m_falloffBox);
    layout->addSpacing(12);

    QPushButton *apply = new QPushButton(i18n("Apply"), w);
    apply->setToolTip(i18n("Apply (Enter)"));
    connect(apply, &QPushButton::clicked, this, [this]() { requestStrokeEnd(); });
    QPushButton *cancel = new QPushButton(i18n("Cancel"), w);
    cancel->setToolTip(i18n("Put the stroke back as drawn (Esc)"));
    connect(cancel, &QPushButton::clicked, this, [this]() { requestStrokeCancellation(); });
    layout->addWidget(apply);
    layout->addWidget(cancel);
    layout->addSpacing(12);

    QLabel *hint = new QLabel(i18n("Drag dots · Shift+click: select more · Double-click the line: add a dot"), w);
    hint->setEnabled(false); // dimmed text
    layout->addWidget(hint);
    layout->addStretch(1);

    m_options = w;
    return w;
}

void GigaToolStrokePoints::slotSpacingChanged(int value)
{
    GigaLastStrokeTransform::setPointSpacing(value);
    GigaLastStrokeTransform *s = session();
    if (!s || m_dragging) return;
    const bool edited = s->path().isEdited();
    if (edited) s->pointsBeginEdit();
    s->path().placeVertices(value / viewScale());
    m_selected.clear();
    m_hover = -1;
    if (edited) s->pointsChanged(); // the bend is kept closely; repaint anyway
    updateOverlay();
}

void GigaToolStrokePoints::slotModePixels()
{
    if (GigaLastStrokeTransform *s = session()) {
        s->pointsSwitchToPixels();
    }
}

void GigaToolStrokePoints::slotSessionChanged()
{
    // Undo may have changed the dot count.
    GigaLastStrokeTransform *s = session();
    if (s) {
        QSet<int> keep;
        for (int i : m_selected) if (i < s->path().vertices().size()) keep.insert(i);
        m_selected = keep;
    }
    updateOverlay();
}

void GigaToolStrokePoints::slotSessionEnded()
{
    m_selected.clear();
    m_hover = -1;
    m_dragging = false;
    // The dots are gone: repaint where they were.
    if (!m_lastOverlay.isEmpty()) updateCanvasPixelRect(m_lastOverlay);
    m_lastOverlay = QRectF();
}

int GigaToolStrokePoints::gigaVertexCount() const
{
    GigaLastStrokeTransform *s = session();
    return s ? s->path().vertices().size() : 0;
}

QPointF GigaToolStrokePoints::gigaVertexImagePos(int index) const
{
    GigaLastStrokeTransform *s = session();
    return s ? s->path().vertexPosition(index) : QPointF();
}

QVector<qreal> GigaToolStrokePoints::proportionalCenters() const
{
    QVector<qreal> centers;
    GigaLastStrokeTransform *s = session();
    if (!s || !m_proportional) return centers;
    if (m_propDrag && m_moved) {
        GigaStrokePath::Move *move = s->path().lastMove();
        return move ? move->centers : centers;
    }
    const QVector<GigaStrokePath::Vertex> &vertices = s->path().vertices();
    for (int i : m_selected) {
        if (i >= 0 && i < vertices.size()) centers << vertices[i].s;
    }
    // Hovering an unselected dot shows what dragging it would pull.
    if (m_hover >= 0 && m_hover < vertices.size() && !m_selected.contains(m_hover)) {
        centers = {vertices[m_hover].s};
    }
    return centers;
}

void GigaToolStrokePoints::setWheelCatcher(bool on)
{
    if (on == m_wheelCatcher) return;
    m_wheelCatcher = on;
    // On the application: it sees the wheel before Krita's canvas input
    // handling (which would zoom). Only installed while dragging.
    if (on) qApp->installEventFilter(this);
    else qApp->removeEventFilter(this);
}

bool GigaToolStrokePoints::eventFilter(QObject *watched, QEvent *event)
{
    if (m_wheelCatcher && event->type() == QEvent::Wheel) {
        QWidget *canvasWidget = canvas() ? canvas()->canvasWidget() : nullptr;
        QWidget *target = qobject_cast<QWidget*>(watched);
        if (canvasWidget && target && (target == canvasWidget || canvasWidget->isAncestorOf(target))) {
            QWheelEvent *wheel = static_cast<QWheelEvent*>(event);
            const int dy = wheel->angleDelta().y();
            if (dy != 0) changeRadius(dy > 0 ? 1 : -1);
            return true; // no zoom while dragging
        }
    }
    return KisTool::eventFilter(watched, event);
}

void GigaToolStrokePoints::changeRadius(int wheelSteps)
{
    qreal radius = m_radius;
    for (int i = 0; i < qAbs(wheelSteps); ++i) radius *= wheelSteps > 0 ? 1.15 : 1.0 / 1.15;
    const int value = qBound(MinRadius, qRound(radius), MaxRadius);
    if (m_radiusBox) m_radiusBox->setValue(value); // -> slotRadiusChanged
    else slotRadiusChanged(value);
}

void GigaToolStrokePoints::slotProportionalToggled(bool on)
{
    m_proportional = on;
    KisConfig(false).writeEntry<bool>(PropKey, on);
    if (m_radiusBox) m_radiusBox->setEnabled(on);
    if (m_falloffBox) m_falloffBox->setEnabled(on);
    updateOverlay();
}

void GigaToolStrokePoints::slotRadiusChanged(int value)
{
    m_radius = qBound(MinRadius, value, MaxRadius);
    KisConfig(false).writeEntry<int>(RadiusKey, m_radius);
    GigaLastStrokeTransform *s = session();
    if (s && m_propDrag && m_moved) {
        // Live while dragging: the move being made gets the new radius.
        if (GigaStrokePath::Move *move = s->path().lastMove()) move->radius = m_radius / viewScale();
        s->pointsChanged(); // live, like dragging
    }
    updateOverlay();
}

void GigaToolStrokePoints::slotFalloffChanged(int index)
{
    m_falloff = qBound(0, index, int(GigaStrokePath::Constant));
    KisConfig(false).writeEntry<int>(FalloffKey, m_falloff);
    updateOverlay();
}

void GigaToolStrokePoints::gigaSetProportional(bool on, int radiusScreenPx, int falloff)
{
    if (m_propButton) m_propButton->setChecked(on);
    else slotProportionalToggled(on);
    if (m_radiusBox) m_radiusBox->setValue(radiusScreenPx);
    else slotRadiusChanged(radiusScreenPx);
    if (m_falloffBox) m_falloffBox->setCurrentIndex(falloff);
    else slotFalloffChanged(falloff);
}
