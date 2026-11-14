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
#include <QPainter>
#include <QStatusBar>
#include <QTabBar>

#include <functional>

namespace {
const char *const SETTING_KEY = "gigaInWindowPanels";
constexpr int SNAP_DISTANCE = 10; // px
// How close to the canvas's left/right edge the pointer must be to dock on
// that side. Smaller than the 30 px tear-off zone, so a panel torn off a dock
// doesn't land straight back in it.
constexpr int SIDE_DOCK_ZONE = 24; // px

// The "it will dock here" highlight. Purely visual: clicks pass through.
class GigaDropHighlight : public QWidget
{
public:
    explicit GigaDropHighlight(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setObjectName(QStringLiteral("GigaDropHighlight"));
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QColor fill = palette().highlight().color();
        QColor edge = fill;
        fill.setAlpha(60);
        p.fillRect(rect(), fill);
        p.setPen(QPen(edge, 2));
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
};

// Application-wide, but it only looks at presses: clicking anywhere in a
// floating panel brings it to the front. Kept separate from the manager so
// the manager's own filter only sees the objects it watches.
class GigaRaiseOnPressFilter : public QObject
{
public:
    explicit GigaRaiseOnPressFilter(std::function<void(QWidget *)> onPress, QObject *parent)
        : QObject(parent), m_onPress(onPress) {}
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const QEvent::Type type = event->type();
        if ((type == QEvent::MouseButtonPress || type == QEvent::TabletPress) && watched->isWidgetType()) {
            m_onPress(static_cast<QWidget *>(watched));
        }
        return false;
    }
private:
    std::function<void(QWidget *)> m_onPress;
};
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
    m_raiseFilter = new GigaRaiseOnPressFilter([this](QWidget *widget) {
        for (const QPointer<GigaFloatingPanel> &panel : m_panels) {
            if (panel && (panel == widget || panel->isAncestorOf(widget))) {
                if (m_panels.last() != panel) {
                    GigaFloatingPanel *p = panel;
                    p->raise();
                    m_panels.removeAll(p);
                    m_panels.append(p);
                    scheduleSave();
                }
                return;
            }
        }
    }, this);
    qApp->installEventFilter(m_raiseFilter);
    // Panel-group tab bars that already exist (new ones: ChildPolished).
    for (QObject *child : mainWindow->children()) {
        if (QTabBar *tabBar = qobject_cast<QTabBar *>(child)) watchTabBar(tabBar);
    }

    connect(KisConfigNotifier::instance(), &KisConfigNotifier::configChanged, this, [this]() {
        setEnabled(settingEnabled());
    });
}

GigaFloatingPanelManager::~GigaFloatingPanelManager()
{
    if (m_saveTimer.isActive()) saveNow();
    if (m_raiseFilter) qApp->removeEventFilter(m_raiseFilter);
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

    // Qt's QMainWindowLayout::tabChanged() calls centralWidget()->raise()
    // whenever a tab in a docked panel group changes; that put the canvas on
    // top of every floating panel. Watch the canvas area and put the panels
    // back on top when that happens (see eventFilter).
    QWidget *central = m_mainWindow->centralWidget();
    if (central && central != m_watchedCentral) {
        if (m_watchedCentral) m_watchedCentral->removeEventFilter(this);
        m_watchedCentral = central;
        central->installEventFilter(this);
    }

    m_mainWindow->removeDockWidget(dock);
    GigaFloatingPanel *panel = new GigaFloatingPanel(dock, m_mainWindow);
    // Inside the frame Qt must not try to drag or float it itself.
    dock->setFeatures(home.features & ~(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable));
    panel->setGeometryFilter([this](const QRect &r, bool moving) { return moving ? snap(r) : keepInside(r); });
    connect(panel, &GigaFloatingPanel::geometryEdited, this, &GigaFloatingPanelManager::scheduleSave);
    panel->setMoveObserver([this](GigaFloatingPanel *p, const QPointF &globalPos, bool finished) {
        onPanelMoved(p, globalPos, finished);
    });
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
    if (!panelFor(dock) || !m_mainWindow) return;
    // Back into its old tab group if one of those panels is still docked,
    // otherwise on its old side.
    const HomeInfo home = m_home.value(dock);
    DropTarget target;
    target.area = home.area;
    for (const QString &id : home.tabSiblings) {
        QDockWidget *candidate = m_mainWindow->dockWidget(id);
        if (candidate && candidate != dock && isDockedTarget(candidate) && !candidate->isHidden()) {
            target.tabWith = candidate;
            break;
        }
    }
    dockInto(dock, target, keepVisible && !dock->isHidden());
}

bool GigaFloatingPanelManager::isDockedTarget(const QDockWidget *dock) const
{
    return dock && m_mainWindow && !isFloating(dock) && !dock->isFloating()
        && dock->parentWidget() == m_mainWindow
        && m_mainWindow->dockWidgetArea(const_cast<QDockWidget *>(dock)) != Qt::NoDockWidgetArea;
}

void GigaFloatingPanelManager::dockInto(QDockWidget *dock, const DropTarget &target, bool show)
{
    GigaFloatingPanel *panel = panelFor(dock);
    if (!panel || !m_mainWindow) return;
    if (panel->isCollapsed()) panel->setCollapsed(false);

    panel->takeDock();
    m_panels.removeAll(panel);
    const HomeInfo home = m_home.take(dock);

    dock->setParent(m_mainWindow);
    dock->setFeatures(home.features);
    const Qt::DockWidgetArea area = target.isValid() ? target.area : home.area;
    if (target.tabWith && isDockedTarget(target.tabWith)) {
        m_mainWindow->tabifyDockWidget(target.tabWith, dock);
    } else if (target.splitFrom && isDockedTarget(target.splitFrom)) {
        m_mainWindow->splitDockWidget(target.splitFrom, dock, target.orientation);
    } else {
        m_mainWindow->addDockWidget(area, dock, target.orientation);
    }
    if (show) {
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

GigaFloatingPanelManager::DropTarget GigaFloatingPanelManager::dropTargetAt(const QPointF &globalPos,
                                                                            const QDockWidget *moving) const
{
    DropTarget target;
    if (!m_mainWindow || !m_mainWindow->centralWidget()) return target;
    const QPoint pos = m_mainWindow->mapFromGlobal(globalPos.toPoint());
    auto rectOf = [this](const QDockWidget *d) { return QRect(d->mapTo(m_mainWindow, QPoint(0, 0)), d->size()); };
    const QRect window = m_mainWindow->rect();
    const QRect canvasArea = m_mainWindow->centralWidget()->geometry();
    auto usable = [this, moving, rectOf, window, canvasArea](const QDockWidget *d) {
        // The toolbox is too slim to share a group with a panel.
        if (!d || d == moving || !d->isVisible() || !isDockedTarget(d)
            || d->objectName() == QLatin1String("ToolBox")) {
            return false;
        }
        // Qt can leave a docked panel "visible" but parked outside the window
        // at a stale spot; only panels really sitting beside the canvas count.
        const QRect r = rectOf(d);
        return !r.isEmpty() && window.contains(r) && !canvasArea.intersects(r);
    };

    // 1. Over a docked panel: join its tab group.
    for (QDockWidget *d : m_watched) {
        if (!usable(d)) continue;
        const QRect r = rectOf(d);
        if (r.contains(pos)) {
            target.area = m_mainWindow->dockWidgetArea(d);
            target.tabWith = d;
            target.highlight = r;
            return target;
        }
    }
    // ...or over a group's row of tabs (it sits outside the panels).
    for (QObject *child : m_mainWindow->children()) {
        QTabBar *tabBar = qobject_cast<QTabBar *>(child);
        if (!tabBar || !tabBar->isVisible() || !tabBar->geometry().contains(pos)) continue;
        QDockWidget *d = dockForTab(tabBar, tabBar->currentIndex());
        if (!usable(d)) continue;
        // Qt keeps spare tab bars around, and a tab's panel isn't always
        // the one drawn under it; only trust a tab row that sits right on
        // top of (or under) its panel, in the same column.
        const QRect panelRect = rectOf(d);
        const QRect tabRect = tabBar->geometry();
        const bool sameColumn = tabRect.left() < panelRect.right() && tabRect.right() > panelRect.left();
        const bool touching = qAbs(tabRect.bottom() + 1 - panelRect.top()) <= 6
            || qAbs(panelRect.bottom() + 1 - tabRect.top()) <= 6;
        if (!sameColumn || !touching) continue;
        target.area = m_mainWindow->dockWidgetArea(d);
        target.tabWith = d;
        target.highlight = panelRect.united(tabRect);
        return target;
    }

    // 2. Near the canvas's left or right edge (or anywhere beside it): a new
    //    spot on that side.
    const QRect canvas = m_mainWindow->centralWidget()->geometry();
    const int width = qBound(200, moving ? moving->width() : 300, 360);
    if (pos.x() < canvas.left() + SIDE_DOCK_ZONE) {
        target.area = Qt::LeftDockWidgetArea;
        QDockWidget *toolBox = m_mainWindow->dockWidget(QStringLiteral("ToolBox"));
        if (toolBox && toolBox->isVisible() && isDockedTarget(toolBox)
            && m_mainWindow->dockWidgetArea(toolBox) == Qt::LeftDockWidgetArea) {
            // Its own column between the toolbox and the canvas, so the
            // toolbox stays slim.
            target.splitFrom = toolBox;
            target.orientation = Qt::Horizontal;
        }
        target.highlight = QRect(canvas.left(), canvas.top(), width, canvas.height());
    } else if (pos.x() > canvas.right() - SIDE_DOCK_ZONE) {
        target.area = Qt::RightDockWidgetArea;
        target.orientation = Qt::Vertical; // under the panels already there
        QRect column;
        for (QDockWidget *d : m_watched) {
            if (usable(d) && m_mainWindow->dockWidgetArea(d) == Qt::RightDockWidgetArea) column |= rectOf(d);
        }
        if (column.isValid()) {
            const int h = qMax(80, column.height() / 3);
            target.highlight = QRect(column.left(), column.bottom() - h + 1, column.width(), h);
        } else {
            target.highlight = QRect(canvas.right() - width + 1, canvas.top(), width, canvas.height());
        }
    }
    return target;
}

void GigaFloatingPanelManager::onPanelMoved(GigaFloatingPanel *panel, const QPointF &globalPos, bool finished)
{
    if (!panel || !panel->dock()) return;
    // Hold Ctrl to move a panel over the docks without docking it.
    const bool noDock = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
    const DropTarget target = noDock ? DropTarget() : dropTargetAt(globalPos, panel->dock());
    if (!finished) {
        if (target.isValid()) showDropHighlight(target.highlight, panel);
        else hideDropHighlight();
        return;
    }
    hideDropHighlight();
    if (!target.isValid()) return;
    // Queued: we're inside the panel's own mouse handling right now.
    QPointer<QDockWidget> dock = panel->dock();
    QTimer::singleShot(0, this, [this, dock, target]() {
        if (dock && isFloating(dock)) dockInto(dock, target, true);
    });
}

void GigaFloatingPanelManager::showDropHighlight(const QRect &rect, GigaFloatingPanel *above)
{
    if (!m_mainWindow) return;
    if (!m_dropHighlight) m_dropHighlight = new GigaDropHighlight(m_mainWindow);
    if (m_dropHighlight->geometry() != rect) m_dropHighlight->setGeometry(rect);
    if (!m_dropHighlight->isVisible()) {
        m_dropHighlight->show();
        m_dropHighlight->raise();
        if (above) above->raise(); // the panel being dragged stays on top
    }
}

void GigaFloatingPanelManager::hideDropHighlight()
{
    if (m_dropHighlight && m_dropHighlight->isVisible()) m_dropHighlight->hide();
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

void GigaFloatingPanelManager::watchTabBar(QTabBar *tabBar)
{
    if (!tabBar) return;
    tabBar->removeEventFilter(this); // no double install
    tabBar->installEventFilter(this);
}

bool GigaFloatingPanelManager::inCanvasZone(const QPointF &globalPos) const
{
    if (!m_mainWindow || !m_mainWindow->centralWidget()) return false;
    // Well inside the canvas area: dropping there can't mean "dock it".
    const QRect zone = m_mainWindow->centralWidget()->geometry().adjusted(30, 30, -30, -30);
    return zone.contains(m_mainWindow->mapFromGlobal(globalPos.toPoint()));
}

QDockWidget *GigaFloatingPanelManager::dockForTab(QTabBar *tabBar, int index) const
{
    if (!tabBar || index < 0 || !m_mainWindow) return nullptr;
    // Qt keeps the dock's address as the tab's data; only trust it if it is
    // one of our docks.
    const quintptr id = tabBar->tabData(index).value<quintptr>();
    for (QDockWidget *dock : m_watched) {
        if (quintptr(dock) == id) return dock;
    }
    // Otherwise match the tab's text to a docked panel's title.
    const QString text = tabBar->tabText(index);
    for (QDockWidget *dock : m_watched) {
        if (dock->windowTitle() == text && m_mainWindow->dockWidgetArea(dock) != Qt::NoDockWidgetArea) return dock;
    }
    return nullptr;
}

void GigaFloatingPanelManager::tearOff(const QPointF &globalPos, bool qtDragStillRunning)
{
    QPointer<QDockWidget> dock = m_drag.dock;
    QPointer<QWidget> source = m_drag.source;
    const QPoint grabOffset = m_drag.grabOffset;
    QSize size = m_drag.size;
    m_drag = TitleDrag();
    if (!dock || !m_mainWindow || isFloating(dock)) return;

    if (qtDragStillRunning && source) {
        // End Qt's own docking drag first, with an ordinary release: Qt then
        // puts the panel back where it was and lets go of the mouse. Only
        // after that do we take the panel out.
        QMouseEvent release(QEvent::MouseButtonRelease, source->mapFromGlobal(globalPos), globalPos,
                            Qt::LeftButton, Qt::NoButton, QGuiApplication::keyboardModifiers());
        QCoreApplication::sendEvent(source, &release);
        if (QWidget *grabber = QWidget::mouseGrabber()) grabber->releaseMouse();
    }

    const int b = GigaFloatingPanel::BORDER;
    size = QSize(qMax(size.width(), 200), qBound(200, size.height(), 600)) + QSize(2 * b, 2 * b);
    const QPoint topLeft = m_mainWindow->mapFromGlobal(globalPos.toPoint()) - grabOffset - QPoint(b, b);
    GigaFloatingPanel *panel = floatPanel(dock, QRect(topLeft, size));
    // Still pressed: keep following the pointer until it's let go.
    if (panel && qtDragStillRunning) panel->beginInteractiveMove(globalPos);
}

bool GigaFloatingPanelManager::handleTearOffDrag(QWidget *source, QDockWidget *pressedDock, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::MouseButtonPress) {
        QMouseEvent *e = static_cast<QMouseEvent *>(event);
        if (e->button() == Qt::LeftButton && pressedDock && !isFloating(pressedDock) && !pressedDock->isFloating()) {
            m_drag.dock = pressedDock;
            m_drag.source = source;
            // Keep the same spot of the title under the pointer once floating.
            m_drag.grabOffset = (source == pressedDock) ? e->position().toPoint() : QPoint(40, 10);
            m_drag.size = pressedDock->size();
            m_drag.active = true;
        }
        return false; // Qt's docking still gets the press
    }
    if (!m_drag.active || m_drag.source != source) return false;

    if (type == QEvent::MouseMove) {
        QMouseEvent *e = static_cast<QMouseEvent *>(event);
        if ((e->buttons() & Qt::LeftButton) && inCanvasZone(e->globalPosition())) {
            tearOff(e->globalPosition(), true);
            return true;
        }
    } else if (type == QEvent::MouseButtonRelease) {
        QMouseEvent *e = static_cast<QMouseEvent *>(event);
        const QPointF globalPos = e->globalPosition();
        if (inCanvasZone(globalPos)) {
            // We missed the moves (e.g. the desktop moved the window): let Qt
            // finish (it snaps the panel back), then float it at the drop.
            QTimer::singleShot(0, this, [this, globalPos, drag = m_drag]() {
                m_drag = drag;
                tearOff(globalPos, false);
            });
        }
        m_drag.active = false;
    }
    return false;
}

bool GigaFloatingPanelManager::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();

    if (watched == m_watchedCentral.data()) {
        // The canvas was raised (by Qt on a tab change, or anything else):
        // floating panels back on top, right away so nothing flickers.
        // Raising the panels doesn't send this event to the canvas again.
        if (type == QEvent::ZOrderChange && !m_panels.isEmpty()) raiseAllFrames();
        return false;
    }

    if (watched == m_mainWindow) {
        if (type == QEvent::Resize) {
            for (const QPointer<GigaFloatingPanel> &panel : m_panels) {
                if (panel) panel->setGeometry(keepInside(panel->geometry()));
            }
        } else if (type == QEvent::ChildPolished) {
            QObject *child = static_cast<QChildEvent *>(event)->child();
            if (QTabBar *tabBar = qobject_cast<QTabBar *>(child)) watchTabBar(tabBar); // a new panel group
            if (!m_panels.isEmpty() && !qobject_cast<GigaFloatingPanel *>(child)
                && child != m_dropHighlight.data()) m_raiseTimer.start();
        }
        return false;
    }

    // Dragging a tab out of a docked panel group.
    if (m_enabled && (type == QEvent::MouseButtonPress || type == QEvent::MouseMove || type == QEvent::MouseButtonRelease)) {
        QTabBar *tabBar = qobject_cast<QTabBar *>(watched);
        if (tabBar && m_mainWindow && tabBar->parentWidget() == m_mainWindow) {
            QDockWidget *pressed = nullptr;
            if (type == QEvent::MouseButtonPress) {
                pressed = dockForTab(tabBar, tabBar->tabAt(static_cast<QMouseEvent *>(event)->position().toPoint()));
            }
            return handleTearOffDrag(tabBar, pressed, event);
        }
    }

    QDockWidget *dock = qobject_cast<QDockWidget *>(watched);
    if (!dock || !m_watched.contains(dock)) return false;

    // Dragging a docked panel by its title bar (Krita's or Photoshop mode's
    // header): Qt docks it as usual, but over the canvas it floats here.
    if (m_enabled && !isFloating(dock) && (type == QEvent::MouseButtonPress || type == QEvent::MouseMove
                                           || type == QEvent::MouseButtonRelease)) {
        QDockWidget *pressed = nullptr;
        if (type == QEvent::MouseButtonPress) {
            QWidget *title = dock->titleBarWidget();
            const QRect titleArea = (title && title->isVisible()) ? title->geometry() : QRect(0, 0, dock->width(), 20);
            if (titleArea.contains(static_cast<QMouseEvent *>(event)->position().toPoint())) pressed = dock;
        }
        if (handleTearOffDrag(dock, pressed, event)) return true;
    }

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
