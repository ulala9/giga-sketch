/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaFloatingPanel.h"
#include "GigaMotion.h"

#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QHash>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>

namespace
{
/**
 * Diagnostics for "moving a panel looks choppy": start Giga Sketch with
 * GIGA_PANEL_TRACE=1 and every panel drag prints one summary line to the
 * terminal (move events, gaps between them, frames, who repainted).
 * Off by default: then the only cost is one bool check per move.
 */
class DragTrace : public QObject
{
public:
    static bool enabled()
    {
        static const bool on = qEnvironmentVariableIsSet("GIGA_PANEL_TRACE");
        return on;
    }
    static DragTrace &instance()
    {
        static DragTrace trace;
        return trace;
    }
    void begin(const QString &name)
    {
        m_name = name;
        m_moves = m_synthesized = m_frames = 0;
        m_gaps.clear();
        m_paints.clear();
        m_device.clear();
        m_clock.start();
        m_lastMoveNs = -1;
        if (!m_active) qApp->installEventFilter(this);
        m_active = true;
    }
    void move(const QMouseEvent *e)
    {
        if (!m_active) return;
        ++m_moves;
        if (e->source() != Qt::MouseEventNotSynthesized) ++m_synthesized; // e.g. made from pen (tablet) events
        if (m_device.isEmpty() && e->pointingDevice()) {
            m_device = e->pointingDevice()->name() + QStringLiteral(" type ") + QString::number(int(e->pointingDevice()->type()));
        }
        const qint64 now = m_clock.nsecsElapsed();
        if (m_lastMoveNs >= 0) m_gaps.append(now - m_lastMoveNs);
        m_lastMoveNs = now;
    }
    void end()
    {
        if (!m_active) return;
        m_active = false;
        qApp->removeEventFilter(this);
        const double ms = m_clock.nsecsElapsed() / 1e6;
        std::sort(m_gaps.begin(), m_gaps.end());
        auto gap = [this](double q) { return m_gaps.isEmpty() ? 0.0 : m_gaps.at(int(q * (m_gaps.size() - 1))) / 1e6; };
        QList<QPair<int, QByteArray>> paints;
        for (auto it = m_paints.constBegin(); it != m_paints.constEnd(); ++it) paints.append({it.value(), it.key()});
        std::sort(paints.begin(), paints.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        QString top;
        for (int i = 0; i < qMin(6, int(paints.size())); ++i) {
            top += QStringLiteral(" %1=%2").arg(QString::fromLatin1(paints[i].second)).arg(paints[i].first);
        }
        fprintf(stderr, "GIGA_PANEL_TRACE %s: %.0f ms, %d moves (%d from pen), gaps ms median %.1f p90 %.1f max %.1f, "
                        "%d frames (%.0f fps), device %s, paints:%s\n",
                qPrintable(m_name), ms, m_moves, m_synthesized, gap(0.5), gap(0.9), gap(1.0), m_frames,
                ms > 0 ? m_frames * 1000.0 / ms : 0.0, qPrintable(m_device), qPrintable(top));
        fflush(stderr);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const QEvent::Type type = event->type();
        if (type == QEvent::Paint) {
            ++m_paints[QByteArray(watched->metaObject()->className())];
        } else if (type == QEvent::UpdateRequest && watched->isWidgetType()
                   && static_cast<QWidget *>(watched)->isWindow()) {
            ++m_frames; // one repaint of a window = one frame
        }
        return false;
    }

private:
    bool m_active {false};
    QString m_name;
    QString m_device;
    int m_moves {0};
    int m_synthesized {0};
    int m_frames {0};
    QList<qint64> m_gaps;
    QHash<QByteArray, int> m_paints;
    QElapsedTimer m_clock;
    qint64 m_lastMoveNs {-1};
};
} // namespace

GigaFloatingPanel::GigaFloatingPanel(QDockWidget *dock, QWidget *mainWindow)
    : QFrame(mainWindow) // a plain child widget, never a desktop window
    , m_dock(dock)
{
    setObjectName(QStringLiteral("GigaFloatingPanel_") + dock->objectName());
    setMouseTracking(true); // resize cursors on the border
    setAutoFillBackground(true);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(BORDER, BORDER, BORDER, BORDER);
    m_layout->setSpacing(0);

    dock->setParent(this);
    m_layout->addWidget(dock);
    // Our filter sees the dock's title-bar drags (the title bar doesn't use
    // plain presses, so they reach the dock) and its show/hide/layout events.
    dock->installEventFilter(this);
    ensureTitleBar();
}

GigaFloatingPanel::~GigaFloatingPanel()
{
    if (m_grabbed) releaseMouse();
}

QDockWidget *GigaFloatingPanel::takeDock()
{
    QDockWidget *dock = m_dock;
    if (!dock) return nullptr;
    m_moving = false; // being docked: not a move that ends in a drop
    finishInteraction();
    dock->removeEventFilter(this);
    m_layout->removeWidget(dock);
    m_dock = nullptr;
    return dock;
}

void GigaFloatingPanel::ensureTitleBar()
{
    if (!m_dock) return;
    QWidget *title = m_dock->titleBarWidget();
    if (title && title->isHidden()) title->show();
}

QRect GigaFloatingPanel::titleArea() const
{
    if (!m_dock) return QRect();
    QWidget *title = m_dock->titleBarWidget();
    if (title && title->isVisible()) return title->geometry();
    return QRect(0, 0, m_dock->width(), 20); // Qt's own title area
}

int GigaFloatingPanel::collapsedHeight() const
{
    const int title = m_dock ? titleArea().height() : 20;
    return title + 2 * BORDER + 2;
}

QSize GigaFloatingPanel::minimumFrameSize() const
{
    QSize min(140, collapsedHeight());
    if (m_dock && !m_collapsed) {
        const QSize dockMin = m_dock->minimumSizeHint();
        min = min.expandedTo(QSize(dockMin.width() + 2 * BORDER, qMin(dockMin.height() + 2 * BORDER, 400)));
        min.setHeight(qMax(min.height(), 80));
    }
    return min;
}

int GigaFloatingPanel::expandedHeight() const
{
    return m_collapsed ? m_expandedHeight : height();
}

void GigaFloatingPanel::setCollapsed(bool collapsed)
{
    if (!m_dock || collapsed == m_collapsed) return;
    QWidget *content = m_dock->widget();
    if (collapsed) {
        m_expandedHeight = height();
        m_collapsed = true;
        if (content) content->hide();
        resize(width(), collapsedHeight());
    } else {
        m_collapsed = false;
        if (content) content->show();
        resize(width(), qMax(m_expandedHeight, minimumFrameSize().height()));
        if (m_filter) setGeometry(m_filter(geometry(), true)); // may have grown off the window
    }
    Q_EMIT geometryEdited();
}

Qt::Edges GigaFloatingPanel::edgesAt(const QPoint &pos) const
{
    Qt::Edges edges;
    const int grip = BORDER + 1;
    if (pos.x() <= grip) edges |= Qt::LeftEdge;
    if (pos.x() >= width() - grip) edges |= Qt::RightEdge;
    if (!m_collapsed) { // a collapsed panel only resizes sideways
        if (pos.y() <= grip) edges |= Qt::TopEdge;
        if (pos.y() >= height() - grip) edges |= Qt::BottomEdge;
    }
    return edges;
}

static Qt::CursorShape cursorFor(Qt::Edges edges)
{
    const bool l = edges & Qt::LeftEdge, r = edges & Qt::RightEdge;
    const bool t = edges & Qt::TopEdge, b = edges & Qt::BottomEdge;
    if ((t && l) || (b && r)) return Qt::SizeFDiagCursor;
    if ((t && r) || (b && l)) return Qt::SizeBDiagCursor;
    if (l || r) return Qt::SizeHorCursor;
    if (t || b) return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

void GigaFloatingPanel::beginInteractiveMove(const QPointF &globalPos)
{
    raise();
    m_moving = true;
    m_resizeEdges = Qt::Edges();
    m_pressGlobal = globalPos;
    m_lastGlobal = globalPos;
    m_startGeometry = geometry();
    if (DragTrace::enabled()) DragTrace::instance().begin(objectName());
    // The button is still held from the flyout or strip; route the rest of
    // the drag (moves and the release) to this frame.
    grabMouse(Qt::ClosedHandCursor);
    m_grabbed = true;
}

void GigaFloatingPanel::moveBy(const QPointF &globalPos)
{
    const QPoint delta = (globalPos - m_pressGlobal).toPoint();
    QRect r = m_startGeometry.translated(delta);
    if (m_filter) r = m_filter(r, true);
    setGeometry(r);
    m_lastGlobal = globalPos;
    if (m_moveObserver) m_moveObserver(this, globalPos, false);
}

void GigaFloatingPanel::resizeBy(const QPointF &globalPos)
{
    const QPoint delta = (globalPos - m_pressGlobal).toPoint();
    const QSize min = minimumFrameSize();
    QRect r = m_startGeometry;
    if (m_resizeEdges & Qt::LeftEdge) r.setLeft(qMin(r.left() + delta.x(), r.right() - min.width() + 1));
    if (m_resizeEdges & Qt::RightEdge) r.setRight(qMax(r.right() + delta.x(), r.left() + min.width() - 1));
    if (m_resizeEdges & Qt::TopEdge) r.setTop(qMin(r.top() + delta.y(), r.bottom() - min.height() + 1));
    if (m_resizeEdges & Qt::BottomEdge) r.setBottom(qMax(r.bottom() + delta.y(), r.top() + min.height() - 1));
    if (m_filter) r = m_filter(r, false);
    setGeometry(r);
}

void GigaFloatingPanel::finishInteraction()
{
    const bool wasActive = m_moving || m_resizeEdges;
    const bool wasMoving = m_moving;
    m_moving = false;
    m_resizeEdges = Qt::Edges();
    if (m_grabbed) {
        releaseMouse();
        m_grabbed = false;
    }
    unsetCursor();
    if (wasMoving && DragTrace::enabled()) DragTrace::instance().end();
    if (wasActive) Q_EMIT geometryEdited();
    // Last, because the observer may dock the panel (queued, but still).
    if (wasMoving && m_moveObserver) m_moveObserver(this, m_lastGlobal, true);
}

bool GigaFloatingPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_dock) return QFrame::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        QMouseEvent *e = static_cast<QMouseEvent *>(event);
        if (e->button() == Qt::LeftButton && titleArea().contains(e->position().toPoint())) {
            raise();
            m_moving = true;
            m_pressGlobal = e->globalPosition();
            m_lastGlobal = m_pressGlobal;
            m_startGeometry = geometry();
            if (DragTrace::enabled()) DragTrace::instance().begin(objectName());
            return true; // never let QDockWidget start its own drag in here
        }
        break;
    }
    case QEvent::MouseMove:
        if (m_moving) {
            if (DragTrace::enabled()) DragTrace::instance().move(static_cast<QMouseEvent *>(event));
            moveBy(static_cast<QMouseEvent *>(event)->globalPosition());
            return true;
        }
        break;
    case QEvent::MouseButtonRelease:
        if (m_moving) {
            m_lastGlobal = static_cast<QMouseEvent *>(event)->globalPosition();
            finishInteraction();
            return true;
        }
        break;
    case QEvent::MouseButtonDblClick: {
        QMouseEvent *e = static_cast<QMouseEvent *>(event);
        if (titleArea().contains(e->position().toPoint())) {
            // Krita's title bar: double-click collapses (Photoshop's header
            // handles its own double-click before it gets here).
            toggleCollapsed();
            return true;
        }
        break;
    }
    case QEvent::LayoutRequest:
        // Krita may hide or swap the title bar (settings, interface modes);
        // a floating panel always needs it as its drag handle.
        QTimer::singleShot(0, this, &GigaFloatingPanel::ensureTitleBar);
        break;
    default:
        break;
    }
    return QFrame::eventFilter(watched, event);
}

void GigaFloatingPanel::mousePressEvent(QMouseEvent *event)
{
    // Only the border reaches the frame itself (the dock covers the rest).
    if (event->button() != Qt::LeftButton) return QFrame::mousePressEvent(event);
    raise();
    m_resizeEdges = edgesAt(event->position().toPoint());
    m_pressGlobal = event->globalPosition();
    m_startGeometry = geometry();
    event->accept();
}

void GigaFloatingPanel::mouseMoveEvent(QMouseEvent *event)
{
    if (m_moving) {
        if (DragTrace::enabled()) DragTrace::instance().move(event);
        moveBy(event->globalPosition()); // a grabbed tear-off drag
    } else if (m_resizeEdges) {
        resizeBy(event->globalPosition());
    } else {
        setCursor(cursorFor(edgesAt(event->position().toPoint())));
    }
    event->accept();
}

void GigaFloatingPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_moving) m_lastGlobal = event->globalPosition();
    finishInteraction();
    event->accept();
}

void GigaFloatingPanel::leaveEvent(QEvent *event)
{
    if (!m_resizeEdges && !m_moving) unsetCursor();
    QFrame::leaveEvent(event);
}

void GigaFloatingPanel::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    GigaMotion::reveal(this);
}

void GigaFloatingPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    // A slightly lighter outline so the panel reads as sitting on top of the canvas.
    p.setPen(palette().mid().color().lighter(130));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    p.setPen(palette().shadow().color());
    p.drawRect(rect().adjusted(1, 1, -2, -2));
}
