/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaFloatingPanelManager.h"

#include "GigaFloatingPanel.h"
#include "KisMainWindow.h"
#include "kis_config.h"
#include "kis_config_notifier.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QMouseEvent>
#include <QStatusBar>

namespace {
const char *const SETTING_KEY = "gigaInWindowPanels";
constexpr int SNAP_DISTANCE = 10; // px
}

GigaFloatingPanelManager::GigaFloatingPanelManager(KisMainWindow *mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_enabled(settingEnabled())
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &GigaFloatingPanelManager::saveNow);

    // New children of the window (docks, toolbars, separators) are stacked
    // on top; put the floating panels back above them.
    m_raiseTimer.setSingleShot(true);
    m_raiseTimer.setInterval(0);
    connect(&m_raiseTimer, &QTimer::timeout, this, &GigaFloatingPanelManager::raiseAllFrames);

    mainWindow->installEventFilter(this);
    qApp->installEventFilter(this); // only presses are looked at (raise on click)

    connect(KisConfigNotifier::instance(), &KisConfigNotifier::configChanged, this, [this]() {
        setEnabled(settingEnabled());
    });
}

GigaFloatingPanelManager::~GigaFloatingPanelManager()
{
    if (m_saveTimer.isActive()) saveNow();
    qApp->removeEventFilter(this);
}

bool GigaFloatingPanelManager::settingEnabled()
{
    return KisConfig(true).readEntry<bool>(SETTING_KEY, true);
}

QString GigaFloatingPanelManager::idOf(const QDockWidget *dock) const
{
    return dock ? dock->objectName() : QString();
}

void GigaFloatingPanelManager::setEnabled(bool enabled)
{
    if (enabled == m_enabled) return;
    if (!enabled) {
        saveNow();               // keep the arrangement for when it's turned back on
        dockAll(true);
        m_enabled = false;
        // Give Qt's own floating back to the panels we took it from.
        for (QDockWidget *dock : std::as_const(m_stripped)) {
            if (dock) dock->setFeatures(dock->features() | QDockWidget::DockWidgetFloatable);
        }
        m_stripped.clear();
    } else {
        m_enabled = true;
        for (QDockWidget *dock : std::as_const(m_watched)) stripFloatable(dock);
        restoreSaved();
    }
}

void GigaFloatingPanelManager::watchDock(QDockWidget *dock)
{
    if (!dock || m_watched.contains(dock)) return;
    m_watched.insert(dock);
    dock->installEventFilter(this);
    // Krita changes features in several places (lock button, title bar
    // setting); keep "floatable" off while we float panels ourselves.
    connect(dock, &QDockWidget::featuresChanged, this, [this, dock]() { stripFloatable(dock); });
    connect(dock, &QDockWidget::topLevelChanged, this, [this, dock]() { stripFloatable(dock); });
    connect(dock, &QObject::destroyed, this, [this, dock]() {
        m_watched.remove(dock);
        m_stripped.remove(dock);
        m_home.remove(dock);
    });
    stripFloatable(dock);
}

void GigaFloatingPanelManager::stripFloatable(QDockWidget *dock)
{
    // Not while it's a Qt floating window (e.g. from before this setting).
    if (!m_enabled || !dock || isFloating(dock) || dock->isFloating()) return;
    const QDockWidget::DockWidgetFeatures f = dock->features();
    if (f & QDockWidget::DockWidgetFloatable) {
        m_stripped.insert(dock);
        dock->setFeatures(f & ~QDockWidget::DockWidgetFloatable);
    }
}

bool GigaFloatingPanelManager::isFloating(const QDockWidget *dock) const
{
    return panelFor(dock) != nullptr;
}

GigaFloatingPanel *GigaFloatingPanelManager::panelFor(const QDockWidget *dock) const
{
    if (!dock) return nullptr;
    for (const QPointer<GigaFloatingPanel> &panel : m_panels) {
        if (panel && panel->dock() == dock) return panel;
    }
    return nullptr;
}

QRect GigaFloatingPanelManager::allowedArea() const
{
    if (!m_mainWindow) return QRect();
    QRect area = m_mainWindow->rect();
    QMenuBar *menuBar = m_mainWindow->menuBar();
    if (menuBar && menuBar->isVisible()) area.setTop(menuBar->geometry().bottom() + 1);
    QStatusBar *statusBar = m_mainWindow->statusBar();
    if (statusBar && statusBar->isVisible()) area.setBottom(statusBar->geometry().top() - 1);
    return area;
}

QRect GigaFloatingPanelManager::keepInside(QRect r) const
{
    const QRect area = allowedArea();
    if (area.isEmpty()) return r;
    r.setWidth(qMin(r.width(), area.width()));
    r.setHeight(qMin(r.height(), area.height()));
    r.moveLeft(qBound(area.left(), r.left(), area.right() - r.width() + 1));
    r.moveTop(qBound(area.top(), r.top(), area.bottom() - r.height() + 1));
    return r;
}

QRect GigaFloatingPanelManager::snap(QRect r) const
{
    // Lines to snap to: the window's usable area, the canvas area and every
    // other visible floating panel.
    QList<int> xs, ys;
    auto addRect = [&xs, &ys](const QRect &rect) {
        xs << rect.left() << rect.right() + 1;
        ys << rect.top() << rect.bottom() + 1;
    };
    addRect(allowedArea());
    if (m_mainWindow && m_mainWindow->centralWidget()) addRect(m_mainWindow->centralWidget()->geometry());
    for (const QPointer<GigaFloatingPanel> &panel : m_panels) {
        if (panel && panel->isVisible() && panel->geometry() != r) addRect(panel->geometry());
    }

    auto best = [](int a, int b, const QList<int> &lines, int *shift) {
        int bestDistance = SNAP_DISTANCE + 1;
        for (int line : lines) {
            if (qAbs(line - a) < bestDistance) { bestDistance = qAbs(line - a); *shift = line - a; }
            if (qAbs(line - b) < bestDistance) { bestDistance = qAbs(line - b); *shift = line - b; }
        }
        return bestDistance <= SNAP_DISTANCE;
    };
    int dx = 0, dy = 0;
    if (best(r.left(), r.right() + 1, xs, &dx)) r.translate(dx, 0);
    if (best(r.top(), r.bottom() + 1, ys, &dy)) r.translate(0, dy);
    return keepInside(r);
}

QRect GigaFloatingPanelManager::defaultGeometryFor(QDockWidget *dock) const
{
    const int b = GigaFloatingPanel::BORDER;
    QSize size = dock->isVisible() ? dock->size() : QSize(320, 440);
    size.setHeight(qMin(size.height(), 600));
    size += QSize(2 * b, 2 * b);
    if (dock->isVisible() && m_mainWindow) {
        // Just beside where it was docked, so it looks like it popped out.
        const QPoint docked = dock->mapTo(m_mainWindow, QPoint(0, 0));
        const bool onRight = docked.x() > m_mainWindow->width() / 2;
        const int x = onRight ? docked.x() - size.width() - 12 : docked.x() + dock->width() + 12;
        return QRect(QPoint(x, docked.y()), size);
    }
    QRect area = (m_mainWindow && m_mainWindow->centralWidget()) ? m_mainWindow->centralWidget()->geometry() : allowedArea();
    QRect r(QPoint(0, 0), size);
    r.moveCenter(area.center());
    return r;
}

GigaFloatingPanel *GigaFloatingPanelManager::floatPanel(QDockWidget *dock, const QRect &geometry)
{
    if (!m_enabled || !dock || !m_mainWindow) return nullptr;
    if (GigaFloatingPanel *existing = panelFor(dock)) return existing;
    if (dock->isFloating()) dock->setFloating(false); // an old Qt floating window: bring it in

    // Remember where it lived, to put it back there later.
    HomeInfo home;
    home.area = m_mainWindow->dockWidgetArea(dock);
    if (home.area == Qt::NoDockWidgetArea) home.area = Qt::RightDockWidgetArea;
    for (QDockWidget *sibling : m_mainWindow->tabifiedDockWidgets(dock)) home.tabSiblings << idOf(sibling);
    home.features = dock->features();
    m_home.insert(dock, home);

    const QRect wanted = geometry.isEmpty() ? defaultGeometryFor(dock) : geometry;

    m_mainWindow->removeDockWidget(dock);
    GigaFloatingPanel *panel = new GigaFloatingPanel(dock, m_mainWindow);
    // Inside the frame Qt must not try to drag or float it itself.
    dock->setFeatures(home.features & ~(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable));
    panel->setGeometryFilter([this](const QRect &r, bool moving) { return moving ? snap(r) : keepInside(r); });
    connect(panel, &GigaFloatingPanel::geometryEdited, this, &GigaFloatingPanelManager::scheduleSave);
    panel->setGeometry(keepInside(wanted));
    m_panels.append(panel);

    dock->show(); // floating is something you do to see a panel
    syncVisibility(panel);
    panel->raise();
    scheduleSave();
    return panel;
}

void GigaFloatingPanelManager::dockPanel(QDockWidget *dock, bool keepVisible)
{
    GigaFloatingPanel *panel = panelFor(dock);
    if (!panel || !m_mainWindow) return;
    if (panel->isCollapsed()) panel->setCollapsed(false);
    const bool wasOpen = !dock->isHidden();

    panel->takeDock();
    m_panels.removeAll(panel);
    const HomeInfo home = m_home.take(dock);

    dock->setParent(m_mainWindow);
    dock->setFeatures(home.features);
    // Back into its old tab group if one of those panels is still docked,
    // otherwise on its old side.
    QDockWidget *sibling = nullptr;
    for (const QString &id : home.tabSiblings) {
        QDockWidget *candidate = m_mainWindow->dockWidget(id);
        if (candidate && candidate != dock && !isFloating(candidate) && !candidate->isFloating()
            && !candidate->isHidden() && m_mainWindow->dockWidgetArea(candidate) != Qt::NoDockWidgetArea) {
            sibling = candidate;
            break;
        }
    }
    if (sibling) {
        m_mainWindow->tabifyDockWidget(sibling, dock);
    } else {
        m_mainWindow->addDockWidget(home.area, dock);
    }
    if (keepVisible && wasOpen) {
        dock->show();
        dock->raise(); // its tab to the front
    } else {
        dock->hide();
    }
    panel->hide();
    panel->deleteLater();
    stripFloatable(dock);
    scheduleSave();
}

void GigaFloatingPanelManager::dockAll(bool keepVisible)
{
    const QList<QPointer<GigaFloatingPanel>> panels = m_panels;
    for (const QPointer<GigaFloatingPanel> &panel : panels) {
        if (panel && panel->dock()) dockPanel(panel->dock(), keepVisible);
    }
}

bool GigaFloatingPanelManager::toggleFloat(QDockWidget *dock)
{
    if (!m_enabled || !dock) return false;
    if (isFloating(dock)) {
        dockPanel(dock);
    } else {
        floatPanel(dock);
    }
    return true;
}

void GigaFloatingPanelManager::raisePanel(QDockWidget *dock)
{
    GigaFloatingPanel *panel = panelFor(dock);
    if (!panel) return;
    if (dock->isHidden()) dock->show(); // syncVisibility shows the frame
    panel->raise();
    m_panels.removeAll(panel);
    m_panels.append(panel);
}

void GigaFloatingPanelManager::raiseAllFrames()
{
    for (const QPointer<GigaFloatingPanel> &panel : m_panels) {
        if (panel && panel->isVisible()) panel->raise();
    }
}

void GigaFloatingPanelManager::syncVisibility(GigaFloatingPanel *panel)
{
    if (!panel || !panel->dock()) return;
    const bool show = !panel->dock()->isHidden() && !m_allHidden;
    if (show == panel->isVisible()) return;
    panel->setVisible(show);
    if (show) {
        panel->ensureTitleBar();
        panel->raise();
    }
}

void GigaFloatingPanelManager::setAllHidden(bool hidden)
{
    m_allHidden = hidden;
    for (const QPointer<GigaFloatingPanel> &panel : m_panels) syncVisibility(panel);
}

void GigaFloatingPanelManager::beginRestore()
{
    m_openBeforeRestore.clear();
    for (const QPointer<GigaFloatingPanel> &panel : m_panels) {
        if (panel && panel->dock() && !panel->dock()->isHidden()) m_openBeforeRestore << panel->dock();
    }
}

void GigaFloatingPanelManager::endRestore()
{
    // Krita's workspace restore hid every panel; bring back the floating
    // ones that were open (the saved workspace doesn't know about them).
    for (const QPointer<QDockWidget> &dock : std::as_const(m_openBeforeRestore)) {
        if (dock && isFloating(dock)) dock->show();
    }
    m_openBeforeRestore.clear();
    for (const QPointer<GigaFloatingPanel> &panel : m_panels) {
        if (!panel || !panel->dock()) continue;
        panel->ensureTitleBar();
        syncVisibility(panel);
    }
}

void GigaFloatingPanelManager::setStorageKey(const QString &key)
{
    if (m_saveTimer.isActive()) saveNow(); // pending changes belong to the old key
    m_storageKey = key;
}

void GigaFloatingPanelManager::scheduleSave()
{
    if (m_restoringSaved || m_storageKey.isEmpty()) return;
    m_saveTimer.start();
}

void GigaFloatingPanelManager::saveNow()
{
    m_saveTimer.stop();
    if (m_storageKey.isEmpty() || !m_enabled) return;
    QJsonArray list;
    for (const QPointer<GigaFloatingPanel> &panel : m_panels) { // bottom to top
        if (!panel || !panel->dock()) continue;
        QDockWidget *dock = panel->dock();
        const HomeInfo home = m_home.value(dock);
        const QRect g = panel->geometry();
        QJsonObject entry;
        entry[QStringLiteral("id")] = idOf(dock);
        entry[QStringLiteral("x")] = g.x();
        entry[QStringLiteral("y")] = g.y();
        entry[QStringLiteral("w")] = g.width();
        entry[QStringLiteral("h")] = panel->expandedHeight();
        entry[QStringLiteral("collapsed")] = panel->isCollapsed();
        entry[QStringLiteral("open")] = !dock->isHidden();
        entry[QStringLiteral("area")] = int(home.area);
        entry[QStringLiteral("tabs")] = QJsonArray::fromStringList(home.tabSiblings);
        list.append(entry);
    }
    KisConfig(false).writeEntry<QString>(m_storageKey,
                                         QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact)));
}

void GigaFloatingPanelManager::restoreSaved()
{
    if (!m_enabled || m_storageKey.isEmpty() || !m_mainWindow) return;
    const QString json = KisConfig(true).readEntry<QString>(m_storageKey, QString());
    const QJsonArray list = QJsonDocument::fromJson(json.toUtf8()).array();
    if (list.isEmpty()) return;

    m_restoringSaved = true; // don't save half-restored states
    for (const QJsonValue &value : list) {
        const QJsonObject entry = value.toObject();
        QDockWidget *dock = m_mainWindow->dockWidget(entry.value(QStringLiteral("id")).toString());
        if (!dock || isFloating(dock)) continue;
        const QRect geometry(entry.value(QStringLiteral("x")).toInt(), entry.value(QStringLiteral("y")).toInt(),
                             entry.value(QStringLiteral("w")).toInt(320), entry.value(QStringLiteral("h")).toInt(440));
        GigaFloatingPanel *panel = floatPanel(dock, geometry);
        if (!panel) continue;
        HomeInfo &home = m_home[dock];
        const int area = entry.value(QStringLiteral("area")).toInt(int(home.area));
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea
            || area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea) {
            home.area = Qt::DockWidgetArea(area);
        }
        const QJsonArray tabs = entry.value(QStringLiteral("tabs")).toArray();
        if (!tabs.isEmpty()) {
            home.tabSiblings.clear();
            for (const QJsonValue &tab : tabs) home.tabSiblings << tab.toString();
        }
        if (entry.value(QStringLiteral("collapsed")).toBool()) {
            panel->setExpandedHeight(geometry.height());
            panel->setCollapsed(true);
            panel->setExpandedHeight(geometry.height());
        }
        if (!entry.value(QStringLiteral("open")).toBool(true)) dock->hide();
    }
    m_restoringSaved = false;
}

bool GigaFloatingPanelManager::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();

    // Clicking anywhere in a floating panel brings it to the front.
    if ((type == QEvent::MouseButtonPress || type == QEvent::TabletPress) && !m_panels.isEmpty()) {
        if (QWidget *widget = qobject_cast<QWidget *>(watched)) {
            for (const QPointer<GigaFloatingPanel> &panel : m_panels) {
                if (panel && (panel == widget || panel->isAncestorOf(widget))) {
                    if (m_panels.last() != panel) {
                        GigaFloatingPanel *p = panel;
                        p->raise();
                        m_panels.removeAll(p);
                        m_panels.append(p);
                        scheduleSave();
                    }
                    break;
                }
            }
        }
    }

    if (watched == m_mainWindow) {
        if (type == QEvent::Resize) {
            for (const QPointer<GigaFloatingPanel> &panel : m_panels) {
                if (panel) panel->setGeometry(keepInside(panel->geometry()));
            }
        } else if (type == QEvent::ChildPolished && !m_panels.isEmpty()) {
            QObject *child = static_cast<QChildEvent *>(event)->child();
            if (!qobject_cast<GigaFloatingPanel *>(child)) m_raiseTimer.start();
        }
        return false;
    }

    QDockWidget *dock = qobject_cast<QDockWidget *>(watched);
    if (!dock || !m_watched.contains(dock)) return false;

    if (type == QEvent::ShowToParent || type == QEvent::HideToParent) {
        // Shown or closed by the user, a menu, the panel strip...: the frame follows.
        if (GigaFloatingPanel *panel = panelFor(dock)) {
            syncVisibility(panel);
            scheduleSave();
        }
    } else if (type == QEvent::MouseButtonDblClick && m_enabled && !isFloating(dock) && !dock->isFloating()) {
        // Double-click on Krita's title bar of a docked panel: float it here.
        // (Photoshop mode's header uses double-click to collapse instead,
        // and handles it before it reaches the dock.)
        QWidget *title = dock->titleBarWidget();
        QMouseEvent *e = static_cast<QMouseEvent *>(event);
        if (title && title->inherits("KoDockWidgetTitleBar") && title->geometry().contains(e->position().toPoint())) {
            floatPanel(dock);
            return true;
        }
    }
    return false;
}
