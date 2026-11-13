/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GIGA_FLOATING_PANEL_H
#define GIGA_FLOATING_PANEL_H

#include <QFrame>
#include <QPointer>

#include <functional>

class QDockWidget;
class QVBoxLayout;

/**
 * Giga Sketch: a panel floating *inside* the main window.
 *
 * Krita's floating panels are separate desktop windows. On Wayland an app
 * can't choose where its windows go, so they never stay where you put them.
 * This frame is an ordinary child widget of the main window instead: it can
 * be placed anywhere over the canvas, on any platform.
 *
 * The whole QDockWidget lives inside the frame (not just its content), so the
 * panel keeps its title bar, buttons and "am I visible?" behaviour. The
 * panel's own title bar is used to drag the frame; the frame's thin border is
 * used to resize it.
 *
 * GigaFloatingPanelManager creates, docks and remembers these frames.
 */
class GigaFloatingPanel : public QFrame
{
    Q_OBJECT
public:
    GigaFloatingPanel(QDockWidget *dock, QWidget *mainWindow);
    ~GigaFloatingPanel() override;

    QDockWidget *dock() const { return m_dock; }

    /// Removes the dock from the frame (it keeps the frame as parent until
    /// the caller reparents it). Afterwards the frame is empty.
    QDockWidget *takeDock();

    bool isCollapsed() const { return m_collapsed; }
    void setCollapsed(bool collapsed);
    void toggleCollapsed() { setCollapsed(!m_collapsed); }
    /// Height to use when expanded (also valid while collapsed).
    int expandedHeight() const;
    void setExpandedHeight(int height) { m_expandedHeight = height; }

    /// Start moving the frame with the pointer that is currently pressed at
    /// @p globalPos (used when tearing a panel off a flyout or the strip).
    void beginInteractiveMove(const QPointF &globalPos);

    /**
     * The manager decides where the frame may go: kept inside the window,
     * snapped to edges. Called with the proposed geometry while moving or
     * resizing; returns the geometry to use.
     */
    void setGeometryFilter(std::function<QRect(const QRect &, bool moving)> filter) { m_filter = filter; }

    /// Make sure the dock's title bar (our drag handle) is showing.
    void ensureTitleBar();

    static constexpr int BORDER = 5; ///< resize border width

Q_SIGNALS:
    /// The user finished moving or resizing the frame.
    void geometryEdited();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QRect titleArea() const; ///< in dock coordinates
    Qt::Edges edgesAt(const QPoint &pos) const;
    void moveBy(const QPointF &globalPos);
    void resizeBy(const QPointF &globalPos);
    void finishInteraction();
    QSize minimumFrameSize() const;
    int collapsedHeight() const;

    QPointer<QDockWidget> m_dock;
    QVBoxLayout *m_layout {nullptr};
    std::function<QRect(const QRect &, bool)> m_filter;

    bool m_moving {false};
    bool m_grabbed {false};
    Qt::Edges m_resizeEdges;
    QPointF m_pressGlobal;
    QRect m_startGeometry;

    bool m_collapsed {false};
    int m_expandedHeight {0};
};

#endif // GIGA_FLOATING_PANEL_H
