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
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QTransform>
#include <cstdio>
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

/**
 * Affine map taking triangle @p src onto triangle @p dst. Unlike a
 * four-corner (perspective) map, an affine map can't blow up: an
 * over-stretched piece just stretches, it never smears to infinity.
 */
bool triangleMap(const QPointF *src, const QPointF *dst, QTransform &out)
{
    const QTransform fromUnit(src[1].x() - src[0].x(), src[1].y() - src[0].y(),
                              src[2].x() - src[0].x(), src[2].y() - src[0].y(),
                              src[0].x(), src[0].y());
    const QTransform toUnitDst(dst[1].x() - dst[0].x(), dst[1].y() - dst[0].y(),
                               dst[2].x() - dst[0].x(), dst[2].y() - dst[0].y(),
                               dst[0].x(), dst[0].y());
    if (qAbs(fromUnit.determinant()) < 1e-6) return false; // flat source triangle
    bool ok = false;
    const QTransform inverse = fromUnit.inverted(&ok);
    if (!ok) return false;
    out = inverse * toUnitDst;
    return true;
}
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
        s->pointsDragStarted();
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
    // Strokes that can be bent on screen are (pointsWarping): nothing
    // repaints until release, so it follows at full frame rate whatever the
    // brush costs. Others repaint live: one repaint at a time, newer moves
    // folded into the next, never waiting on the painting threads.
    if (!s->pointsWarping()) s->pointsChanged();
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
    if (s && moved) s->pointsDragFinished(); // one real repaint of the final shape
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
        qreal margin = 12.0 / viewScale();
        if (m_proportional) margin = qMax(margin, (m_radius + 60.0) / viewScale());
        if (s->pointsWarping()) margin += s->pointsBrushHalfWidth();
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

    if (s->pointsWarping()) {
        static const bool trace = qEnvironmentVariableIsSet("GIGA_POINTS_TRACE");
        QElapsedTimer clock;
        if (trace) clock.start();
        paintWarp(gc, s);
        if (trace) fprintf(stderr, "GIGA_POINTS bent preview drawn in %.2f ms\n", clock.nsecsElapsed() / 1e6);
    }

    // Live repaint (strokes that can't be bent on screen): a faint guide of
    // the new path while the repaint is a step behind.
    if (!s->pointsWarping() && ((m_dragging && m_moved) || s->pointsRepainting())) {
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
    paintCircles(gc, s);
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
        if (!s->pointsWarping()) s->pointsChanged(); // live, like dragging
    }
    m_radiusChanged.start();
    QTimer::singleShot(1300, this, [this]() { updateOverlay(); }); // hide the label
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

bool GigaToolStrokePoints::gigaWarping() const
{
    GigaLastStrokeTransform *s = session();
    return s && s->pointsWarping();
}

void GigaToolStrokePoints::ensureWarpImage(GigaLastStrokeTransform *s)
{
    const int serial = s->pointsWarpSerial();
    // About screen resolution (a bit more), never more than the stroke's own.
    const qreal want = qMin<qreal>(1.0, viewScale() * 1.25);
    if (serial == m_warpImageSerial && !m_warpImage.isNull() && qAbs(want - m_warpImageScale) < 0.1) return;
    m_warpImage = QImage();
    m_warpImageSerial = serial;
    KisPaintDeviceSP device = s->pointsWarpDevice();
    if (!device) return;
    const QRect rect = device->exactBounds();
    if (rect.isEmpty()) return;
    QImage image = device->convertToQImage(nullptr, rect.x(), rect.y(), rect.width(), rect.height());
    if (want < 0.95) {
        image = image.scaled(qMax(1, qRound(rect.width() * want)), qMax(1, qRound(rect.height() * want)),
                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    m_warpImage = image;
    m_warpOrigin = rect.topLeft();
    m_warpImageScale = qreal(image.width()) / rect.width();
}

void GigaToolStrokePoints::paintWarp(QPainter &gc, GigaLastStrokeTransform *s)
{
    ensureWarpImage(s);
    if (m_warpImage.isNull()) return;

    const GigaStrokePath &path = s->path();
    const QVector<QPointF> from = path.bentPolyline(s->pointsWarpShape()); // as last painted
    const QVector<QPointF> to = path.bentPolyline();                       // as now
    const QVector<qreal> &lengths = path.pathLengths();
    const int n = qMin(from.size(), qMin(to.size(), lengths.size()));
    if (n < 1) return;
    const qreal w = s->pointsBrushHalfWidth();

    // Cross-sections every ~8 screen px along the line.
    const qreal step = qMax<qreal>(1.0, 8.0 / viewScale());
    QVector<int> picks;
    picks << 0;
    qreal last = 0.0;
    for (int i = 1; i < n - 1; ++i) {
        if (lengths[i] - last >= step) {
            picks << i;
            last = lengths[i];
        }
    }
    if (n > 1) picks << n - 1;

    auto tangent = [n](const QVector<QPointF> &v, int i) {
        const QPointF d = v[qMin(n - 1, i + 2)] - v[qMax(0, i - 2)];
        const qreal len = std::hypot(d.x(), d.y());
        return len > 1e-6 ? d / len : QPointF(1.0, 0.0);
    };
    struct Section { QPointF from, fromT, to, toT; };
    QVector<Section> sections;
    for (int i : picks) sections << Section{from[i], tangent(from, i), to[i], tangent(to, i)};
    // Caps: the brush reaches past both ends of the line.
    {
        const Section a = sections.first();
        sections.prepend(Section{a.from - a.fromT * w, a.fromT, a.to - a.toT * w, a.toT});
        const Section b = sections.last();
        sections.append(Section{b.from + b.fromT * w, b.fromT, b.to + b.toT * w, b.toT});
    }

    // Stroke pixel -> preview image pixel.
    auto toImage = [this](const QPointF &p) { return (p - m_warpOrigin) * m_warpImageScale; };
    auto normal = [](const QPointF &t) { return QPointF(-t.y(), t.x()); };

    const QTransform base = gc.transform();
    gc.save();
    gc.setOpacity(s->pointsLayerOpacity());
    gc.setRenderHint(QPainter::SmoothPixmapTransform, true);
    gc.setRenderHint(QPainter::Antialiasing, false); // shared edges, no seams

    QPainterPath unchanged; // pieces that didn't move: one draw
    unchanged.setFillRule(Qt::WindingFill);
    for (int k = 0; k + 1 < sections.size(); ++k) {
        const Section &a = sections[k];
        const Section &b = sections[k + 1];
        const QPointF na = normal(a.fromT), nb = normal(b.fromT);
        const QPolygonF src({toImage(a.from + na * w), toImage(b.from + nb * w),
                             toImage(b.from - nb * w), toImage(a.from - na * w)});
        const bool same = QLineF(a.from, a.to).length() < 0.05 && QLineF(b.from, b.to).length() < 0.05
                && QPointF::dotProduct(a.fromT, a.toT) > 0.99999 && QPointF::dotProduct(b.fromT, b.toT) > 0.99999;
        if (same) {
            unchanged.addPolygon(src);
            unchanged.closeSubpath();
            continue;
        }
        const QPointF ma = normal(a.toT), mb = normal(b.toT);
        const QPointF dst[4] = {pixelToView(a.to + ma * w), pixelToView(b.to + mb * w),
                                pixelToView(b.to - mb * w), pixelToView(a.to - ma * w)};
        // Two triangles per piece (0-1-2 and 0-2-3), each mapped affinely.
        static const int tri[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (const auto &t : tri) {
            const QPointF s3[3] = {src[t[0]], src[t[1]], src[t[2]]};
            const QPointF d3[3] = {dst[t[0]], dst[t[1]], dst[t[2]]};
            QTransform map;
            if (!triangleMap(s3, d3, map)) continue;
            gc.setTransform(map * base);
            QPainterPath clip;
            clip.addPolygon(QPolygonF({s3[0], s3[1], s3[2]}));
            clip.closeSubpath();
            gc.setClipPath(clip);
            const QRectF area = clip.boundingRect();
            gc.drawImage(area, m_warpImage, area);
        }
    }
    if (!unchanged.isEmpty()) {
        // Image pixel -> view, without bending.
        const QRectF all(QPointF(0, 0), QSizeF(m_warpImage.size()));
        const QPointF s3[3] = {all.topLeft(), all.topRight(), all.bottomLeft()};
        QPointF d3[3];
        for (int i = 0; i < 3; ++i) d3[i] = pixelToView(m_warpOrigin + s3[i] / m_warpImageScale);
        QTransform map;
        if (triangleMap(s3, d3, map)) {
            gc.setTransform(map * base);
            gc.setClipPath(unchanged);
            const QRectF area = unchanged.boundingRect() & all;
            gc.drawImage(area, m_warpImage, area);
        }
    }
    gc.restore();
}

void GigaToolStrokePoints::paintCircles(QPainter &gc, GigaLastStrokeTransform *s)
{
    // Only while a dot is held (pressed), like Blender's circle while grabbing.
    if (!m_proportional || !m_dragging) return;
    const QVector<qreal> centers = proportionalCenters();
    if (centers.isEmpty()) return;
    gc.save();
    gc.setRenderHint(QPainter::Antialiasing, true);
    gc.setBrush(Qt::NoBrush);
    const qreal r = m_radius; // the radius is set in screen px
    const bool label = m_radiusChanged.isValid() && m_radiusChanged.elapsed() < 1200;
    for (qreal at : centers) {
        const QPointF c = pixelToView(s->path().bentPointAt(at));
        // Same two-tone line as the brush outline: readable on any colour.
        gc.setPen(QPen(QColor(0, 0, 0, 140), 3.0));
        gc.drawEllipse(c, r, r);
        gc.setPen(QPen(QColor(255, 255, 255, 220), 1.2));
        gc.drawEllipse(c, r, r);
        if (label) {
            const QString text = i18n("%1 px", m_radius);
            const QPointF at2 = c + QPointF(r * 0.72 + 6, -r * 0.72 - 6);
            QFont font = gc.font();
            font.setBold(true);
            gc.setFont(font);
            gc.setPen(QColor(0, 0, 0, 170));
            gc.drawText(at2 + QPointF(1, 1), text);
            gc.setPen(QColor(255, 255, 255));
            gc.drawText(at2, text);
        }
    }
    gc.restore();
}
