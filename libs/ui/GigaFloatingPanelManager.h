/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_FLOATING_PANEL_MANAGER_H
#define GIGA_FLOATING_PANEL_MANAGER_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QDockWidget>

class QTabBar;

class KisMainWindow;
class GigaFloatingPanel;

/**
 * Giga Sketch: floats panels inside the main window (see GigaFloatingPanel).
 *
 * One manager per main window. It
 *  - turns a docked panel into a floating frame and back,
 *  - keeps frames inside the window and snaps them to edges and each other,
 *  - hides them with the rest of the panels (Welcome page, Tab, workspaces),
 *  - remembers them (per interface mode, through setStorageKey()).
 *
 * While it is enabled, panels lose Qt's "floatable" feature so Qt never makes
 * a separate desktop window for them; the float button, the panel menu and a
 * double-click on Krita's title bar float them in-window instead.
 */
class GigaFloatingPanelManager : public QObject
{
    Q_OBJECT
public:
    explicit GigaFloatingPanelManager(KisMainWindow *mainWindow);
    ~GigaFloatingPanelManager() override;

    /// Setting "Float panels inside the main window" (on by default).
    static bool settingEnabled();
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    /// Start managing a dock (call once for every dock of the window).
    void watchDock(QDockWidget *dock);

    bool isFloating(const QDockWidget *dock) const;
    GigaFloatingPanel *panelFor(const QDockWidget *dock) const;

    /**
     * Float @p dock in the window. @p geometry is in main-window coordinates;
     * an empty rect puts it beside where it was docked.
     */
    GigaFloatingPanel *floatPanel(QDockWidget *dock, const QRect &geometry = QRect());
    /// Put a floating panel back where it was docked.
    void dockPanel(QDockWidget *dock, bool keepVisible = true);
    /// Dock every floating panel (hidden unless @p keepVisible).
    void dockAll(bool keepVisible = false);
    /// Float if docked, dock if floating. False when disabled (not handled).
    bool toggleFloat(QDockWidget *dock);
    /// Show (if closed) and bring to the front.
    void raisePanel(QDockWidget *dock);

    /// Hide every floating panel temporarily (Welcome page, Tab), or bring
    /// back the ones that were open.
    void setAllHidden(bool hidden);
    /// Around Krita's workspace restore, which hides every panel first.
    void beginRestore();
    void endRestore();

    /// Where to remember the floating panels (one key per interface mode).
    void setStorageKey(const QString &key);
    void saveNow();
    /// Float the panels remembered under the current key.
    void restoreSaved();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct HomeInfo {
        Qt::DockWidgetArea area {Qt::RightDockWidgetArea};
        QStringList tabSiblings; ///< ids of the panels it was tabbed with
        QDockWidget::DockWidgetFeatures features;
    };

    /// Where a floating panel would dock if let go at a point.
    struct DropTarget {
        Qt::DockWidgetArea area {Qt::NoDockWidgetArea};
        QPointer<QDockWidget> tabWith;   ///< join this docked panel's tab group
        QPointer<QDockWidget> splitFrom; ///< or sit beside this one
        Qt::Orientation orientation {Qt::Vertical};
        QRect highlight;                 ///< what to light up, in window coordinates
        bool isValid() const { return area != Qt::NoDockWidgetArea; }
    };
    DropTarget dropTargetAt(const QPointF &globalPos, const QDockWidget *moving) const;
    bool isDockedTarget(const QDockWidget *dock) const;
    void onPanelMoved(GigaFloatingPanel *panel, const QPointF &globalPos, bool finished);
    void showDropHighlight(const QRect &rect, GigaFloatingPanel *above);
    void hideDropHighlight();
    /// Take a floating panel out of its frame and dock it as @p target says.
    void dockInto(QDockWidget *dock, const DropTarget &target, bool show);

    void syncVisibility(GigaFloatingPanel *panel);
    void stripFloatable(QDockWidget *dock);
    QRect allowedArea() const;
    QRect keepInside(QRect r) const;
    QRect snap(QRect r) const;
    QRect defaultGeometryFor(QDockWidget *dock) const;
    void raiseAllFrames();
    void scheduleSave();
    QString idOf(const QDockWidget *dock) const;

    // Tearing a docked panel off by dragging its title bar or its tab.
    bool inCanvasZone(const QPointF &globalPos) const;
    QDockWidget *dockForTab(QTabBar *tabBar, int index) const;
    void watchTabBar(QTabBar *tabBar);
    void tearOff(const QPointF &globalPos, bool qtDragStillRunning);
    bool handleTearOffDrag(QWidget *source, QDockWidget *pressedDock, QEvent *event);

    QPointer<KisMainWindow> m_mainWindow;
    bool m_enabled {true};
    bool m_allHidden {false};
    QList<QPointer<GigaFloatingPanel>> m_panels; ///< bottom to top
    QHash<const QDockWidget *, HomeInfo> m_home;
    QSet<QDockWidget *> m_watched;
    QSet<QDockWidget *> m_stripped; ///< docks whose Floatable feature we removed
    QList<QPointer<QDockWidget>> m_openBeforeRestore;
    QString m_storageKey;
    QTimer m_saveTimer;
    QTimer m_raiseTimer;
    bool m_restoringSaved {false};
    QObject *m_raiseFilter {nullptr}; ///< app-wide, presses only
    QPointer<QWidget> m_dropHighlight;
    /// The canvas area (central widget), watched for being raised: Qt raises
    /// it whenever a panel-group tab changes, which would bury the panels.
    QPointer<QWidget> m_watchedCentral;

    struct TitleDrag {
        QPointer<QDockWidget> dock;
        QPointer<QWidget> source;   ///< the dock (title bar) or a panel-group tab bar
        QPoint grabOffset;          ///< where the title was grabbed, in dock coordinates
        QSize size;                 ///< the panel's docked size
        bool active {false};
    } m_drag;
};

#endif // GIGA_FLOATING_PANEL_MANAGER_H
