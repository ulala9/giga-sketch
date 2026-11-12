/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_PROCREATE_MODE_H
#define GIGA_PROCREATE_MODE_H

#include <QByteArray>
#include <QObject>
#include <QPointer>

class QAction;
class QSlider;
class QToolBar;
class QToolButton;
class KisMainWindow;

/**
 * Giga Sketch: a Procreate-style mode for the main window, switched with
 * View > Procreate Mode (F12).
 *
 * Entering it saves the current ("Studio") window layout and hides all
 * panels, toolbars, the menu bar and the status bar. In their place:
 *  - a slim top bar: Gallery, Actions, Adjustments, Selection, Transform on
 *    the left; Brush, Smudge, Eraser, Layers and a colour dot on the right.
 *    Brush/Layers/Color open as popovers (the panel flyouts).
 *  - a side bar on the left edge: brush size slider, colour picker,
 *    opacity slider, undo and redo.
 * Leaving it restores the Studio layout exactly. The mode is remembered
 * across restarts.
 */
class GigaProcreateMode : public QObject
{
    Q_OBJECT
public:
    explicit GigaProcreateMode(KisMainWindow *mainWindow);

    /// The View-menu action that toggles the mode.
    QAction *toggleAction() const { return m_toggle; }

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
    QByteArray m_studioState;

    QPointer<QToolBar> m_topBar;
    QPointer<class GigaSideBarPill> m_sideBar; ///< floating pill over the canvas
    QSlider *m_sizeSlider {nullptr};
    QSlider *m_opacitySlider {nullptr};
    QToolButton *m_colorDot {nullptr};
    bool m_updatingSliders {false};
    bool m_resourcesConnected {false};
};

#endif // GIGA_PROCREATE_MODE_H
