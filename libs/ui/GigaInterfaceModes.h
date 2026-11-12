/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_INTERFACE_MODES_H
#define GIGA_INTERFACE_MODES_H

#include <QHash>
#include <QObject>
#include <QPointer>

class QAction;
class QActionGroup;
class QDockWidget;
class QMenu;
class QWidget;
class KisMainWindow;
class GigaProcreateMode;

/**
 * Giga Sketch: three interface modes, cycled with F9 or picked from
 * View > Interface Mode.
 *  - Studio: classic Krita look: Krita's own menus and toolbars (with the
 *    user's customisations), Krita wording, its own theme and layout.
 *  - Photoshop: Photoshop-style panels (tabs on top, panel headers with
 *    collapse and a panel menu, whole tab groups drag together).
 *  - Procreate: the minimal Procreate-style layout (GigaProcreateMode).
 * Each mode remembers its own window layout; the current mode is
 * remembered across restarts.
 */
class GigaInterfaceModes : public QObject
{
    Q_OBJECT
public:
    enum Mode { Studio = 0, Photoshop = 1, Procreate = 2, ModeCount = 3 };

    explicit GigaInterfaceModes(KisMainWindow *mainWindow);

    /// View > Interface Mode submenu.
    QMenu *menu() const { return m_menu; }

    /// Switch to the mode saved last time (call once the window is set up).
    void restoreSavedMode();

    /// The mode saved last time (read before the window builds its menus).
    static int savedMode();

public Q_SLOTS:
    void setMode(int mode);
    void nextMode();

private:
    void setPhotoshopPanels(bool on);
    /// Switch modes. @p atStartup: the window already shows this mode's
    /// layout (Krita restored it), so don't save or restore layouts.
    void switchTo(int mode, bool atStartup);
    void applyModeTheme(int mode);

    KisMainWindow *m_mainWindow {nullptr};
    GigaProcreateMode *m_procreate {nullptr};
    QMenu *m_menu {nullptr};
    QActionGroup *m_group {nullptr};
    QAction *m_modeActions[ModeCount] {};
    QAction *m_next {nullptr};
    int m_mode {Studio};

    // Photoshop panels: Krita's own title bars, to put back afterwards.
    QHash<QDockWidget *, QPointer<QWidget>> m_originalTitleBars;
    QString m_originalStyleSheet;
    int m_originalDockOptions {0};
    bool m_photoshopPanelsOn {false};
};

#endif // GIGA_INTERFACE_MODES_H
