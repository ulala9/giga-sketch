/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_PROCREATE_MODE_H
#define GIGA_PROCREATE_MODE_H

#include <QByteArray>
#include <QObject>
#include <QList>
#include <QPointer>

class QAction;
class QSlider;
class QToolBar;
class QToolButton;
class KisMainWindow;

/**
 * Layout: a top bar with Gallery/Actions/Adjustments/Selection/Transform on
 * the left, and a vertical bar on the right edge with Paint/Smudge/Erase/
 * Layers/color.
 *
 * Giga Sketch: a Procreate-style mode for the main window, switched with
 * View > Procreate Mode (F9).
 *
 * Entering it saves the current ("Studio") window layout and hides all
 * panels, toolbars, the menu bar and the status bar. In their place:
 *  - a slim top bar: Gallery, Actions, Adjustments, Selection, Transform on
 *    the left; Brush, Smudge, Eraser, Layers and a color dot on the right.
 *    Brush/Layers/Color open as popovers (the panel flyouts).
 *  - a side bar on the left edge: brush size slider, color picker,
 *    opacity slider, undo and redo.
 * Leaving it restores the Studio layout exactly. The mode is remembered
 * across restarts.
 */
class GigaProcreateMode : public QObject
{
    Q_OBJECT
public:
    /// @p nextModeAction is listed in the Actions menu (to leave the mode).
    GigaProcreateMode(KisMainWindow *mainWindow, QAction *nextModeAction);

    /// Hide our bars again, e.g. after another mode restored a layout.
    void hideBars();

    bool isActive() const { return m_active; }

public Q_SLOTS:
    void setActive(bool active);

private Q_SLOTS:
    void updateSideBar();
    void updateColorDot();

private:
    void buildBars();
    QToolButton *addButton(QToolBar *bar, const QString &icon, const QString &text, const QString &toolTip);
    void triggerAction(const QString &name);
    void activateTool(const QString &toolId);
    void brushClicked(QToolButton *button);
    void smudgeClicked(QToolButton *button);
    void eraserClicked(QToolButton *button);
    void connectResources();
    /// Open a Procreate-style popover with @p content under @p anchor.
    void showPopover(QWidget *content, QWidget *anchor);
    /// Brush library popover (second tap on Paint / Smudge / Erase).
    void showBrushLibrary(QWidget *anchor);

    KisMainWindow *m_mainWindow {nullptr};
    QAction *m_toggle {nullptr};
    bool m_active {false};

    QPointer<QToolBar> m_topBar;
    QPointer<QToolBar> m_rightBar; ///< vertical bar on the right edge
    QList<QPointer<QToolBar>> m_hiddenBars; ///< toolbars we hid, shown again on leaving
    QSlider *m_sizeSlider {nullptr};
    QSlider *m_opacitySlider {nullptr};
    QToolButton *m_colorDot {nullptr};
    bool m_updatingSliders {false};
    bool m_resourcesConnected {false};
};

#endif // GIGA_PROCREATE_MODE_H
