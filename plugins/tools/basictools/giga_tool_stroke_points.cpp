/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "giga_tool_stroke_points.h"

#include <QApplication>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QToolButton>

#include <KoPointerEvent.h>
#include <kis_cursor.h>
#include <kis_slider_spin_box.h>

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
}

GigaToolStrokePoints::GigaToolStrokePoints(KoCanvasBase *canvas)
    : KisTool(canvas, KisCursor::arrowCursor())
{
    setObjectName(QStringLiteral("tool_giga_stroke_points"));
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
    updateOverlay();
    m_selected.clear();
    m_dragging = false;
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
    }
    for (int i : m_selected) {
        if (i >= 0 && i < m_startOffsets.size()) s->path().setOffset(i, m_startOffsets[i] + delta);
    }
    updateOverlay();
}

void GigaToolStrokePoints::release()
{
    GigaLastStrokeTransform *s = session();
    const bool moved = m_dragging && m_moved;
    m_dragging = false;
    m_moved = false;
    if (s && moved) s->pointsChanged(); // repaint the stroke along the new path
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

    // While dragging (and until the repaint lands): a thin line of the new path.
    if ((m_dragging && m_moved) || s->pointsRepainting()) {
        QPolygonF line;
        const QVector<QPointF> points = path.bentPolyline();
        line.reserve(points.size());
        for (const QPointF &p : points) line << pixelToView(p);
        gc.setBrush(Qt::NoBrush);
        gc.setPen(QPen(QColor(0, 0, 0, 120), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        gc.drawPolyline(line);
        gc.setPen(QPen(QColor(255, 255, 255, 230), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        gc.drawPolyline(line);
    }

    const QColor accent = qApp->palette().color(QPalette::Highlight);
    for (int i = 0; i < path.vertices().size(); ++i) {
        const QPointF c = pixelToView(path.vertexPosition(i));
        const qreal r = (i == m_hover) ? DotHoverRadius : DotRadius;
        gc.setPen(QPen(QColor(0, 0, 0, 170), 1.5));
        gc.setBrush(m_selected.contains(i) ? accent : QColor(255, 255, 255));
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
