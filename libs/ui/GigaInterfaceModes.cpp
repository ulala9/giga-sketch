/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaInterfaceModes.h"

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QRubberBand>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QTabBar>
#include <QTabWidget>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include "GigaPanelTitleBar.h"
#include "GigaProcreateMode.h"
#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "kis_floating_message.h"
#include "kis_action.h"
#include "kis_action_manager.h"
#include "kis_config.h"

namespace {

QString stateKey(int mode) { return QStringLiteral("gigaModeState%1").arg(mode); }
QString themeKey(int mode) { return QStringLiteral("gigaModeTheme%1").arg(mode); }
QString statusBarKey(int mode) { return QStringLiteral("gigaModeStatusBar%1").arg(mode); }

// Photoshop-style panel chrome: compact dark tabs on top of each group,
// the active tab lighter, thin dark splitters between panels.
const char *PHOTOSHOP_TAB_CSS =
    "QTabBar{background:#232325;}"
    "QTabBar::tab{background:#2b2b2d;color:#a9a9a9;padding:3px 10px;margin:0;"
    "border:0;border-right:1px solid #1b1b1d;}"
    "QTabBar::tab:selected{background:#3a3a3d;color:#f0f0f0;}"
    "QTabBar::tab:hover{color:#e0e0e0;}";

} // namespace

GigaInterfaceModes::GigaInterfaceModes(KisMainWindow *mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
{
    // F9 cycles Studio -> Photoshop -> Procreate. On the window itself so it
    // also works in Procreate mode, where the menu bar is hidden.
    m_next = new QAction(i18nc("@action:inmenu", "Next Interface Mode"), this);
    m_next->setShortcut(QKeySequence(Qt::Key_F9));
    m_next->setShortcutContext(Qt::WindowShortcut);
    connect(m_next, &QAction::triggered, this, &GigaInterfaceModes::nextMode);
    m_mainWindow->addAction(m_next);

    m_menu = new QMenu(i18nc("@title:menu", "Interface Mode"), m_mainWindow);
    m_group = new QActionGroup(this);
    m_group->setExclusive(true);
    m_names[Studio] = i18nc("interface mode", "Studio");
    m_names[Photoshop] = i18nc("interface mode", "Photoshop");
    m_names[Procreate] = i18nc("interface mode", "Procreate");
    for (int mode = 0; mode < ModeCount; mode++) {
        QAction *action = m_menu->addAction(m_names[mode]);
        action->setCheckable(true);
        action->setChecked(mode == Studio);
        m_group->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode]() { setMode(mode); });
        m_modeActions[mode] = action;
    }
    m_menu->addSeparator();
    m_menu->addAction(m_next);

    m_procreate = new GigaProcreateMode(m_mainWindow, m_next);

    // Qt creates its docking drop indicator as a child of the main window
    // the first time a panel is dragged; catch it then (see eventFilter).
    m_mainWindow->installEventFilter(this);
}

bool GigaInterfaceModes::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_mainWindow && event->type() == QEvent::ChildPolished) {
        // A new child of the window is ready (fully built, about to show).
        // (ChildAdded comes too early: the child isn't constructed yet.)
        QObject *child = static_cast<QChildEvent *>(event)->child();
        if (QRubberBand *band = qobject_cast<QRubberBand *>(child)) {
            band->removeEventFilter(this);
            band->installEventFilter(this); // Qt's docking drop indicator
        } else if (m_photoshopPanelsOn && qobject_cast<QTabBar *>(child)) {
            styleTabBars(); // a new panel group got its tabs
        }
    } else if (event->type() == QEvent::Paint && m_mode == Photoshop) {
        // Photoshop mode: while dragging a panel, the spot where it will
        // land is drawn as a clear blue box instead of Qt's faint outline.
        QRubberBand *band = qobject_cast<QRubberBand *>(watched);
        if (band) {
            QPainter p(band);
            const QRect r = band->rect().adjusted(1, 1, -2, -2);
            p.fillRect(r, QColor(49, 140, 231, 70));
            p.setPen(QPen(QColor(49, 140, 231), 2));
            p.drawRect(r);
            return true; // we painted it; skip Qt's own drawing
        }
    }
    return QObject::eventFilter(watched, event);
}

void GigaInterfaceModes::showStandardBars(int mode)
{
    m_mainWindow->menuBar()->show();

    // Status bar: each mode remembers View > Show Status Bar; off in Studio
    // by default. Krita keeps this in its own setting too (it re-applies it
    // when a document opens), so set that and the menu check mark as well.
    const bool statusBarOn = KisConfig(true).readEntry<bool>(statusBarKey(mode), mode == Photoshop);
    KisConfig(false).setShowStatusBar(statusBarOn);
    // (The Welcome page keeps its bars hidden; they come back with a document.)
    const bool welcome = m_mainWindow->gigaShowingWelcomePage();
    if (!welcome) m_mainWindow->statusBar()->setVisible(statusBarOn);
    if (KisViewManager *viewManager = m_mainWindow->viewManager()) {
        if (QAction *toggle = viewManager->actionManager()->actionByName(QStringLiteral("showStatusBar"))) {
            // On the Welcome page only update the check mark (toggling would show the bar).
            QSignalBlocker blocker(welcome ? toggle : nullptr);
            toggle->setChecked(statusBarOn);
        }
    }
    // The options bar is the toolbar that holds the brush/tool controls.
    if (welcome) return;
    Q_FOREACH (QToolBar *bar, m_mainWindow->findChildren<QToolBar *>()) {
        if (bar->objectName() == QLatin1String("BrushesAndStuff")) bar->show();
    }
}

void GigaInterfaceModes::syncSavedWindowSettings()
{
    // Same group and keys KMainWindow uses (see kmainwindow.cpp).
    KConfigGroup cg = KSharedConfig::openConfig()->group(QStringLiteral("MainWindow"));
    cg.writeEntry("State", m_mainWindow->saveState().toBase64());
    cg.writeEntry("MenuBar", m_mainWindow->menuBar()->isHidden() ? "Disabled" : "Enabled");
    cg.writeEntry("StatusBar", m_mainWindow->statusBar()->isHidden() ? "Disabled" : "Enabled");
}

void GigaInterfaceModes::showModeMessage(int mode)
{
    // Same floating message as zooming. Needs an open document (the
    // message is drawn over the canvas).
    KisViewManager *viewManager = m_mainWindow->viewManager();
    if (!viewManager) return;
    viewManager->showFloatingMessage(i18nc("floating message after switching interface mode", "%1 mode", m_names[mode]),
                                     QIcon(), 1500, KisFloatingMessage::Medium);
}

int GigaInterfaceModes::savedMode()
{
    KisConfig cfg(true);
    int saved = cfg.readEntry<int>("gigaInterfaceMode", -1);
    if (saved < 0) {
        // Older setting from when there was only an on/off Procreate mode.
        saved = cfg.readEntry<bool>("gigaProcreateMode", false) ? Procreate : Studio;
    }
    return (saved >= 0 && saved < ModeCount) ? saved : Studio;
}

void GigaInterfaceModes::restoreSavedMode()
{
    // m_mode starts as Studio, but the window was built for the saved mode
    // (menus) and Krita has already restored the layout used last time,
    // which belongs to the saved mode. So only the chrome is switched here.
    m_mode = -1;
    switchTo(savedMode(), true);
}

void GigaInterfaceModes::nextMode()
{
    setMode((m_mode + 1) % ModeCount);
}

void GigaInterfaceModes::setMode(int mode)
{
    switchTo(mode, false);
}

void GigaInterfaceModes::switchTo(int mode, bool atStartup)
{
    if (mode < 0 || mode >= ModeCount) return;
    if (mode == m_mode) {
        m_modeActions[mode]->setChecked(true);
        return;
    }
    // One clean change instead of several partial redraws.
    m_mainWindow->setUpdatesEnabled(false);
    KisConfig cfg(false);
    // On the Welcome page Krita hides every panel, so what's on screen is not
    // the real layout. Then work with the saved layouts instead: the one in
    // [MainWindow] State is applied when a document opens.
    const bool welcome = m_mainWindow->gigaShowingWelcomePage();
    KConfigGroup windowConfig = KSharedConfig::openConfig()->group(QStringLiteral("MainWindow"));

    // 1. Remember the layout and theme of the mode we're leaving.
    if (!atStartup && m_mode >= 0) {
        const QByteArray leaving = welcome
            ? QByteArray::fromBase64(windowConfig.readEntry("State", QByteArray()))
            : m_mainWindow->saveState();
        if (!leaving.isEmpty()) {
            cfg.writeEntry<QByteArray>(stateKey(m_mode), leaving.toBase64());
        }
        cfg.writeEntry<QString>(themeKey(m_mode), m_mainWindow->gigaCurrentThemeName());
        if (m_mode != Procreate) {
            cfg.writeEntry<bool>(statusBarKey(m_mode), cfg.showStatusBar());
        }
    }

    // 2. Leave it.
    if (m_mode == Procreate) m_procreate->setActive(false);
    if (m_mode == Photoshop) setPhotoshopPanels(false);

    // 3. Enter the new one. Menus and toolbars first (Studio uses Krita's,
    //    which rebuilds them), so the layout below can place the toolbars.
    m_mainWindow->gigaSetClassicInterface(mode == Studio);
    const QByteArray saved = atStartup ? QByteArray()
        : QByteArray::fromBase64(cfg.readEntry<QByteArray>(stateKey(mode), QByteArray()));
    if (mode == Procreate) {
        m_procreate->setActive(true);
    } else {
        if (mode == Photoshop) setPhotoshopPanels(true);
        if (welcome) {
            // Don't bring panels onto the Welcome page; hand the layout to
            // Krita to apply when a document opens.
            if (!saved.isEmpty()) windowConfig.writeEntry("State", saved.toBase64());
        } else if (!saved.isEmpty()) {
            m_mainWindow->restoreState(saved);
        } else if (mode == Photoshop && !atStartup) {
            m_mainWindow->applyGigaLayout(); // first visit: start from the Photoshop-like arrangement
        }
        m_procreate->hideBars(); // a restored layout may not know about them
        showStandardBars(mode);
    }
    if (welcome && mode == Procreate && !saved.isEmpty()) {
        windowConfig.writeEntry("State", saved.toBase64()); // Procreate's own (panel-free) layout
    }
    applyModeTheme(mode);
    if (!welcome) {
        syncSavedWindowSettings();
    } else {
        // Never save the Welcome page's hidden panels, but do record which
        // bars this mode shows, for when a document opens.
        const bool barsOn = mode != Procreate;
        windowConfig.writeEntry("MenuBar", barsOn ? "Enabled" : "Disabled");
        windowConfig.writeEntry("StatusBar", (barsOn && cfg.showStatusBar()) ? "Enabled" : "Disabled");
    }
    if (!atStartup) showModeMessage(mode);

    m_mode = mode;
    m_modeActions[mode]->setChecked(true);
    cfg.writeEntry<int>("gigaInterfaceMode", mode);
    m_mainWindow->setUpdatesEnabled(true);
}

void GigaInterfaceModes::applyModeTheme(int mode)
{
    // Studio keeps the classic Krita theme; the others use Giga Dark. Pick
    // another theme while in a mode and that mode remembers it.
    KisConfig cfg(true);
    const QString fallback = mode == Studio ? QStringLiteral("Krita blender") : QStringLiteral("Giga Dark");
    const QString theme = cfg.readEntry<QString>(themeKey(mode), fallback);
    if (!theme.isEmpty() && theme != m_mainWindow->gigaCurrentThemeName()) {
        m_mainWindow->gigaSetTheme(theme);
    }
}

void GigaInterfaceModes::setPhotoshopPanels(bool on)
{
    if (on == m_photoshopPanelsOn) return;
    m_photoshopPanelsOn = on;

    if (on) {
        m_originalDockOptions = int(m_mainWindow->dockOptions());

        // Tabs on top of each group, like Photoshop; dragging a group's header
        // moves the whole group.
        m_mainWindow->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
        m_mainWindow->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks
                                     | QMainWindow::AllowNestedDocks | QMainWindow::GroupedDragging);
        styleTabBars();

        Q_FOREACH (QDockWidget *dock, m_mainWindow->dockWidgets()) {
            if (dock->objectName() == QLatin1String("ToolBox")) continue; // keep the toolbox slim
            m_originalTitleBars.insert(dock, dock->titleBarWidget());
            if (dock->isVisible()) {
                attachHeader(dock);
            } else {
                // Most panels are hidden: give them a header when first shown.
                m_pendingHeaders.insert(dock, connect(dock, &QDockWidget::visibilityChanged, this, [this, dock](bool visible) {
                    if (visible && m_photoshopPanelsOn) attachHeader(dock);
                }));
            }
        }
    } else {
        for (auto it = m_pendingHeaders.begin(); it != m_pendingHeaders.end(); ++it) disconnect(it.value());
        m_pendingHeaders.clear();

        // Put Krita's own title bars back exactly as they were; keep ours
        // (hidden) for next time.
        for (auto it = m_originalTitleBars.begin(); it != m_originalTitleBars.end(); ++it) {
            QDockWidget *dock = it.key();
            if (!dock) continue;
            QWidget *ours = m_ourTitleBars.value(dock);
            if (ours && dock->titleBarWidget() == ours) {
                static_cast<GigaPanelTitleBar *>(ours)->setCollapsed(false);
                dock->setTitleBarWidget(it.value());
                if (it.value()) it.value()->show();
                ours->hide();
            }
        }
        m_originalTitleBars.clear();

        for (const auto &styled : m_styledTabBars) {
            if (styled.first) styled.first->setStyleSheet(styled.second);
        }
        m_styledTabBars.clear();

        m_mainWindow->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::South);
        m_mainWindow->setDockOptions(QMainWindow::DockOptions(m_originalDockOptions));
    }
}

void GigaInterfaceModes::attachHeader(QDockWidget *dock)
{
    if (!dock || !m_photoshopPanelsOn) return;
    QWidget *header = m_ourTitleBars.value(dock);
    if (!header) {
        header = new GigaPanelTitleBar(dock, m_mainWindow);
        m_ourTitleBars.insert(dock, header);
    }
    if (dock->titleBarWidget() != header) {
        dock->setTitleBarWidget(header);
        header->show();
    }
    if (m_pendingHeaders.contains(dock)) disconnect(m_pendingHeaders.take(dock));
}

void GigaInterfaceModes::styleTabBars()
{
    // The panel group tab bars are direct children of the main window (the
    // document tabs live inside the canvas area and aren't touched).
    Q_FOREACH (QTabBar *bar, m_mainWindow->findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
        bool done = false;
        for (const auto &styled : m_styledTabBars) done = done || styled.first == bar;
        if (done) continue;
        m_styledTabBars.append({QPointer<QWidget>(bar), bar->styleSheet()});
        bar->setStyleSheet(QString::fromLatin1(PHOTOSHOP_TAB_CSS));
    }
}
