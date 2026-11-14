/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_PANEL_TITLE_BAR_H
#define GIGA_PANEL_TITLE_BAR_H

#include <QPointer>
#include <QWidget>

class QDockWidget;
class QLabel;
class QMenu;
class QMainWindow;
class QToolButton;

/**
 * Giga Sketch Photoshop mode: a Photoshop-style panel header.
 *
 * A thin dark strip with the panel name, a collapse button and a panel menu
 * (Collapse, Float, Close, Close Tab Group). Double-click collapses or
 * expands the panel. Dragging the strip moves the panel, because mouse
 * events the header doesn't use go on to the QDockWidget, which handles
 * docking, floating and (with GroupedDragging) moving whole tab groups.
 */
class GigaPanelTitleBar : public QWidget
{
public:
    GigaPanelTitleBar(QDockWidget *dock, QMainWindow *mainWindow);

    /// Shrink the panel to just this header, or bring its content back.
    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    /// Floating (inside the window, or as Qt's own floating window)?
    bool isFloatingHere() const;
    /// Float/Dock button tooltip follows the panel's state.
    void updateFloatButton();

    QPointer<QDockWidget> m_dock;
    QPointer<QMainWindow> m_mainWindow;
    QLabel *m_title {nullptr};
    QToolButton *m_collapse {nullptr};
    QToolButton *m_float {nullptr};
    QToolButton *m_close {nullptr};
    QMenu *m_contextMenu {nullptr};
    bool m_collapsed {false};
    int m_expandedMaxHeight {QWIDGETSIZE_MAX};
};

#endif // GIGA_PANEL_TITLE_BAR_H
