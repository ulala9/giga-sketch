/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaSelfTest.h"

#include "GigaFloatingPanel.h"
#include "GigaFloatingPanelManager.h"
#include "GigaInterfaceModes.h"
#include "KisMainWindow.h"
#include "KisDocument.h"
#include "KisPart.h"
#include "kis_config.h"
#include "GigaReferenceBoard.h"
#include "KisReferenceImage.h"
#include "KisReferenceImagesLayer.h"
#include "KisView.h"
#include "kis_canvas2.h"
#include "KisViewManager.h"
#include "kis_action_manager.h"
#include "kis_action.h"
#include "kis_node_manager.h"
#include "kis_coordinates_converter.h"
#include <kis_image.h>
#include <kis_paint_device.h>
#include <kis_layer.h>
#include <KoToolManager.h>
#include <KoToolRegistry.h>
#include <KoPointerEvent.h>
#include <kis_tool.h>
#include <GigaBranding.h>
#include <kundo2stack.h>
#include <klocalizedstring.h>
#include <KoCanvasResourceProvider.h>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QThread>

#include <cmath>
#include <cstdio>
#include <functional>

// Mark synthetic events as coming from the system, the way QtTest and
// QtWebEngine do (QEvent befriends this class name). Some tools cancel a
// stroke whose release is not "spontaneous" (e.g. KisToolOutlineBase).
class QSpontaneKeyEvent
{
public:
    static void setSpontaneous(QEvent *event) { event->setSpontaneous(); }
};

namespace
{
int s_failures = 0;

// Krita's message handler hides qDebug/qWarning; write straight to stderr.
void report(bool ok, const char *name, const QString &detail)
{
    if (!ok) ++s_failures;
    fprintf(stderr, "GIGA_SELFTEST %s %s: %s\n", ok ? "PASS" : "FAIL", name, qPrintable(detail));
}

void info(const QString &text)
{
    fprintf(stderr, "GIGA_SELFTEST INFO %s\n", qPrintable(text));
}

QString rectText(const QRect &r)
{
    return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

/// Let queued work (deleteLater, singleShot(0), layouts) run.
void pump(int rounds = 5)
{
    for (int i = 0; i < rounds; ++i) {
        qApp->processEvents();
        // The tests run inside a timer callback, where processEvents() leaves
        // deleteLater() requests alone; a real return to the event loop
        // would run them. Without this, widgets Qt already retired (e.g.
        // old panel-group tab bars) linger and get poked by the test.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

/**
 * Press at @p localStart on @p target, move to @p globalEnd in @p steps
 * steps, run @p beforeRelease (to look at in-drag state), release.
 */
void drag(QWidget *target, const QPoint &localStart, const QPoint &globalEnd, int steps = 5,
          const std::function<void()> &beforeRelease = {})
{
    const QPoint g0 = target->mapToGlobal(localStart);
    QMouseEvent press(QEvent::MouseButtonPress, localStart, g0, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &press);
    // Like Qt: while a widget grabs the mouse, it gets the events.
    QPointer<QWidget> pressed = target;
    auto receiver = [&pressed]() -> QWidget * {
        QWidget *grabber = QWidget::mouseGrabber();
        return grabber ? grabber : pressed.data();
    };
    for (int i = 1; i <= steps; ++i) {
        const QPoint g = g0 + (globalEnd - g0) * i / steps;
        QWidget *r = receiver();
        if (!r) return;
        QMouseEvent move(QEvent::MouseMove, r->mapFromGlobal(g), g, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(r, &move);
    }
    if (beforeRelease) beforeRelease();
    QWidget *r = receiver();
    if (!r) return;
    QMouseEvent release(QEvent::MouseButtonRelease, r->mapFromGlobal(globalEnd), globalEnd, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(r, &release);
    pump();
}

QWidget *dropHighlight(KisMainWindow *w)
{
    QWidget *h = w->findChild<QWidget *>(QStringLiteral("GigaDropHighlight"), Qt::FindDirectChildrenOnly);
    return (h && h->isVisible()) ? h : nullptr;
}

// ---------------------------------------------------------------------------
// Tests. Add new ones here and to the table in runNamed().

/// In-window floating panels: float, drag back into the docks, highlight size.
void testPanels(KisMainWindow *w)
{
    GigaFloatingPanelManager *m = w->gigaFloatingPanels();
    QWidget *canvas = w->centralWidget();
    if (!m || !canvas) { report(false, "setup", QStringLiteral("no manager or canvas")); return; }
    w->applyGigaLayout();
    pump();

    QDockWidget *hist = w->dockWidget(QStringLiteral("History"));
    QDockWidget *layers = w->dockWidget(QStringLiteral("KisLayerBox"));
    QDockWidget *chan = w->dockWidget(QStringLiteral("ChannelDocker"));
    if (!hist || !layers || !chan) { report(false, "setup", QStringLiteral("missing panels")); return; }
    info(QStringLiteral("window %1x%2 canvas %3").arg(w->width()).arg(w->height()).arg(rectText(canvas->geometry())));

    const QRect mid(canvas->geometry().center() - QPoint(150, 150), QSize(300, 300));
    const QRect window = w->rect();
    auto highlightOk = [w, window](const char *name) {
        QWidget *h = dropHighlight(w);
        report(h && window.contains(h->geometry()) && h->width() <= 400, name,
               h ? QStringLiteral("highlight ") + rectText(h->geometry()) : QStringLiteral("no highlight"));
    };
    const QPoint grab(30, 8); // on the title bar

    // Float and dock with the API.
    m->floatPanel(hist, mid);
    pump();
    report(m->isFloating(hist), "float", QStringLiteral("History floating"));
    m->dockPanel(hist);
    pump();
    report(!m->isFloating(hist) && w->tabifiedDockWidgets(layers).contains(hist), "dock-home",
           QStringLiteral("History back in the Layers group"));

    // Drop onto a docked panel: joins its tab group.
    m->floatPanel(hist, mid);
    layers->raise(); // its tab to the front, so its position is current
    pump();
    drag(hist, grab, layers->mapToGlobal(layers->rect().center()), 5, [&] { highlightOk("onto-layers-highlight"); });
    report(!m->isFloating(hist) && w->tabifiedDockWidgets(layers).contains(hist), "onto-layers",
           QStringLiteral("tabbed with Layers"));

    // Drop at the canvas's left edge: own column beside the toolbox.
    m->floatPanel(chan, mid);
    pump();
    drag(chan, grab, canvas->mapToGlobal(QPoint(5, canvas->height() / 2)), 5, [&] { highlightOk("left-edge-highlight"); });
    report(!m->isFloating(chan) && w->dockWidgetArea(chan) == Qt::LeftDockWidgetArea, "left-edge",
           QStringLiteral("area %1").arg(int(w->dockWidgetArea(chan))));

    // Drop in the middle: stays floating, no highlight.
    m->floatPanel(chan, mid);
    pump();
    bool highlightInMiddle = true;
    drag(chan, grab, canvas->mapToGlobal(canvas->rect().center() + QPoint(40, 40)), 5,
         [&] { highlightInMiddle = dropHighlight(w) != nullptr; });
    report(m->isFloating(chan) && !highlightInMiddle, "middle", QStringLiteral("still floating"));

    // Drop at the right edge (with Layers floating, like the user's setup).
    m->floatPanel(layers, mid.translated(-200, 0));
    pump();
    drag(chan, grab, canvas->mapToGlobal(QPoint(canvas->width() - 5, canvas->height() - 20)), 5,
         [&] { highlightOk("right-edge-highlight"); });
    report(!m->isFloating(chan) && w->dockWidgetArea(chan) == Qt::RightDockWidgetArea, "right-edge",
           QStringLiteral("area %1").arg(int(w->dockWidgetArea(chan))));

    // Tear a docked panel off by its title bar.
    m->dockAll(true);
    pump();
    drag(chan, grab, canvas->mapToGlobal(canvas->rect().center()), 8);
    report(m->isFloating(chan), "tear-off", QStringLiteral("Channels floating after title drag"));
    m->dockAll(true);
}

/// The main window's tab bar that has a tab titled @p title (a docked panel group).
QTabBar *tabBarWith(KisMainWindow *w, const QString &title, int *index)
{
    for (QTabBar *bar : w->findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!bar->isVisible()) continue;
        for (int i = 0; i < bar->count(); ++i) {
            if (bar->tabText(i) == title) { *index = i; return bar; }
        }
    }
    return nullptr;
}

void clickTab(QTabBar *bar, int index)
{
    const QPoint p = bar->tabRect(index).center();
    const QPoint g = bar->mapToGlobal(p);
    QMouseEvent press(QEvent::MouseButtonPress, p, g, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(bar, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, p, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(bar, &release);
    pump();
}

/// Leave the Welcome page (it hides every panel): open a new document.
KisDocument *openTestDocument(KisMainWindow *w)
{
    KisDocument *doc = KisPart::instance()->createDocument();
    KisPart::instance()->addDocument(doc, false);
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    doc->newImage(QStringLiteral("selftest"), 2000, 1500, cs, KoColor(Qt::white, cs), KisConfig::RASTER_LAYER, 1, QString(), 1.0);
    w->showDocument(doc);
    pump(20);
    return doc;
}

/// Autosaves of never-saved documents go to $HOME (the test's throw-away
/// home) and must be named after Giga Sketch, not "krita-...", so stock
/// Krita's startup recovery never offers (or deletes) them.
void testAutosaveName(KisMainWindow *w)
{
    // Startup recovery: was a recovery dialog opened for files already in $HOME?
    // (selftest.sh starts with an empty home; put files there by hand to check
    // that stock Krita's "krita-*" autosaves are ignored and ours are offered.)
    bool recoveryDialog = false;
    for (QWidget *top : QApplication::topLevelWidgets()) {
        recoveryDialog = recoveryDialog || (top->isVisible() && top->inherits("KisAutoSaveRecoveryDialog"));
    }
    const QStringList patterns = {QStringLiteral("*-autosave.kra"), QStringLiteral(".*-autosave.kra")};
    const QStringList before = QDir::home().entryList(patterns, QDir::Files | QDir::Hidden);
    info(QStringLiteral("autosaves in home at start: %1; recovery dialog open: %2")
             .arg(before.isEmpty() ? QStringLiteral("none") : before.join(QStringLiteral(", ")))
             .arg(recoveryDialog ? QStringLiteral("yes") : QStringLiteral("no")));

    KisDocument *doc = openTestDocument(w);
    if (!doc) { report(false, "setup", QStringLiteral("no document")); return; }
    doc->setModified(true);
    QMetaObject::invokeMethod(doc, "slotAutoSave"); // what the autosave timer calls

    QStringList found;
    QElapsedTimer waited;
    waited.start();
    while (found.isEmpty() && waited.elapsed() < 15000) { // saving runs in the background
        pump(5);
        found = QDir::home().entryList(patterns, QDir::Files | QDir::Hidden);
        for (const QString &name : before) found.removeAll(name); // only the new one
    }
    const QString expected = GigaBranding::appName() + QStringLiteral("-");
    bool ok = !found.isEmpty();
    for (const QString &name : found) ok = ok && (name.startsWith(expected) || name.startsWith(QLatin1Char('.') + expected));
    report(ok, "autosave-name", found.isEmpty() ? QStringLiteral("no autosave file after 15 s") : found.join(QStringLiteral(", ")));
}

/// User report: moving a floating panel looks choppy. Times each step of a
/// drag across the canvas: the move itself (geometry, snapping, dock-drop
/// check) and the repaint that follows.
void testPanelMove(KisMainWindow *w)
{
    GigaFloatingPanelManager *m = w->gigaFloatingPanels();
    QWidget *canvas = w->centralWidget();
    if (!m || !canvas) { report(false, "setup", QStringLiteral("no manager or canvas")); return; }
    openTestDocument(w);
    w->applyGigaLayout();
    pump(20);

    const QStringList ids = {QStringLiteral("History"), QStringLiteral("KisLayerBox"), QStringLiteral("PresetDocker"),
                             QStringLiteral("WideGamutColorSelector")};
    for (int variant = 0; variant < 2; ++variant)
    for (const QString &id : ids) {
        const bool collapsed = variant == 1; // header only: is it the content's repaint?
        QDockWidget *dock = w->dockWidget(id);
        if (!dock) { info(QStringLiteral("no panel ") + id); continue; }
        const QPoint c = canvas->geometry().center();
        m->floatPanel(dock, QRect(c - QPoint(400, 250), QSize(320, 450)));
        pump(20);
        GigaFloatingPanel *frame = qobject_cast<GigaFloatingPanel *>(dock->parentWidget());
        if (!frame) { report(false, qPrintable(id), QStringLiteral("did not float")); continue; }

        if (collapsed) { frame->setCollapsed(true); pump(10); }
        const QPoint grab(30, 8);
        const QPoint g0 = dock->mapToGlobal(grab);
        QMouseEvent press(QEvent::MouseButtonPress, grab, g0, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(dock, &press);
        const int steps = 90;
        qint64 moveNs = 0, paintNs = 0, worstFrameNs = 0;
        QElapsedTimer total;
        total.start();
        for (int i = 1; i <= steps; ++i) {
            const QPoint g = g0 + QPoint(i * 6, (i % 30) * 3);
            QElapsedTimer t;
            t.start();
            QMouseEvent move(QEvent::MouseMove, dock->mapFromGlobal(g), g, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(dock, &move);
            const qint64 a = t.nsecsElapsed();
            qApp->processEvents(); // the repaint (and the flush to the screen)
            const qint64 b = t.nsecsElapsed();
            moveNs += a;
            paintNs += b - a;
            worstFrameNs = qMax(worstFrameNs, b);
        }
        const qint64 totalNs = total.nsecsElapsed();
        QMouseEvent release(QEvent::MouseButtonRelease, dock->mapFromGlobal(g0), g0, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(dock, &release);
        pump(10);
        info(QStringLiteral("%1%7: per step move %2 ms, repaint %3 ms, worst step %4 ms, total %5 ms for %6 steps")
                 .arg(id).arg(moveNs / steps / 1e6, 0, 'f', 2).arg(paintNs / steps / 1e6, 0, 'f', 2)
                 .arg(worstFrameNs / 1e6, 0, 'f', 1).arg(totalNs / 1e6, 0, 'f', 0).arg(steps)
                 .arg(collapsed ? QStringLiteral(" (collapsed)") : QString()));
        if (collapsed) frame->setCollapsed(false);
        m->dockAll(true);
        pump(10);
    }
    report(true, "panelmove", QStringLiteral("timings above"));
}

/// User report: dragging a panel "feels like it's on a grid" with several
/// panels floating. A panel far away must not pull the dragged one; a panel
/// right beside it still must. (Ctrl = no snapping is checked by hand:
/// synthetic events don't update the keyboard modifiers.)
void testPanelSnap(KisMainWindow *w)
{
    GigaFloatingPanelManager *m = w->gigaFloatingPanels();
    QWidget *canvas = w->centralWidget();
    if (!m || !canvas) { report(false, "setup", QStringLiteral("no manager or canvas")); return; }
    openTestDocument(w);
    w->applyGigaLayout();
    pump(20);
    QDockWidget *movingDock = w->dockWidget(QStringLiteral("History"));
    QDockWidget *otherDock = w->dockWidget(QStringLiteral("PresetDocker"));
    if (!movingDock || !otherDock) { report(false, "setup", QStringLiteral("missing panels")); return; }

    const QPoint c = canvas->geometry().center();
    info(QStringLiteral("canvas %1").arg(rectText(canvas->geometry())));
    m->floatPanel(movingDock, QRect(c + QPoint(-350, -250), QSize(200, 200)));
    m->floatPanel(otherDock, QRect(c + QPoint(50, 150), QSize(200, 150)));
    pump(20);
    auto frameOf = [](QDockWidget *dock) { return qobject_cast<GigaFloatingPanel *>(dock->parentWidget()); };
    GigaFloatingPanel *moving = frameOf(movingDock);
    GigaFloatingPanel *other = frameOf(otherDock);
    if (!moving || !other) { report(false, "setup", QStringLiteral("did not float")); return; }

    // Drag the History frame so its left edge ends up `wantLeft`.
    auto dragLeftTo = [&](int wantLeft) {
        const QPoint grab(30, 8);
        const QPoint delta(wantLeft - moving->geometry().left(), 0);
        drag(movingDock, grab, movingDock->mapToGlobal(grab) + delta, 20);
        pump(10);
    };

    // 1. The other panel is far below: its left edge 5 px away must not pull.
    const QRect far = other->geometry();
    dragLeftTo(far.left() - 5);
    report(moving->geometry().left() == far.left() - 5, "far-panel-no-snap",
           QStringLiteral("moving %1, other %2").arg(rectText(moving->geometry()), rectText(far)));

    // 2. The other panel sits beside it (same height band): end 5 px short of
    //    touching it, it must snap to touch.
    other->setGeometry(QRect(QPoint(moving->geometry().right() + 300, moving->geometry().top()), QSize(200, 200)));
    pump(10);
    const QRect beside = other->geometry();
    dragLeftTo(beside.left() - 5 - moving->width());
    report(moving->geometry().right() + 1 == beside.left(), "beside-panel-snaps",
           QStringLiteral("moving %1, other %2").arg(rectText(moving->geometry()), rectText(beside)));

    // 3. Slow drag in 1 px steps in open space: the panel must follow every
    //    pixel (it used to stick to its own previous position and jump).
    other->move(other->geometry().translated(0, 400).topLeft());
    pump(10);
    {
        const QPoint grab(30, 8);
        QPoint pointer = movingDock->mapToGlobal(grab);
        const QPoint start = moving->geometry().topLeft();
        drag(movingDock, grab, pointer + QPoint(25, 0), 25);
        pump(10);
        const int moved = moving->geometry().left() - start.x();
        report(moved == 25, "slow-drag-follows",
               QStringLiteral("pointer moved 25 px in 1 px steps, panel moved %1 px").arg(moved));
    }

    m->dockAll(true);
    pump(10);
}

QTabBar *tabBarWith(KisMainWindow *w, const QString &title, int *index);

/// Crash report: Photoshop mode -> Studio, then drag a panel-group tab.
/// Steps are printed before they run, so a crash shows where it happened.
void testModeSwitchTabDrag(KisMainWindow *w)
{
    GigaInterfaceModes *modes = w->findChild<GigaInterfaceModes *>();
    if (!modes) { report(false, "setup", QStringLiteral("no modes")); return; }
    openTestDocument(w);
    const QList<int> order = {int(GigaInterfaceModes::Studio), int(GigaInterfaceModes::Photoshop), int(GigaInterfaceModes::Studio),
                              int(GigaInterfaceModes::Photoshop)};
    for (int mode : order) {
        modes->setMode(mode);
        pump(20);
        const QString tag = mode == GigaInterfaceModes::Studio ? QStringLiteral("studio") : QStringLiteral("photoshop");
        auto groupBars = [w]() {
            int n = 0;
            for (QTabBar *bar : w->findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
                if (bar->isVisible() && bar->count() >= 2) ++n;
            }
            return n;
        };
        if (groupBars() == 0) { // no panel groups in this layout: make some
            w->applyGigaLayout();
            pump(20);
        }
        int bars = 0;
        for (QTabBar *bar : w->findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (!bar->isVisible() || bar->count() < 2) continue;
            ++bars;
            info(QStringLiteral("%1: drag tab '%2' of %3 tabs (movable=%4)").arg(tag, bar->tabText(0)).arg(bar->count()).arg(bar->isMovable()));
            // Sideways along the bar: reordering, no tear-off.
            const QRect r = bar->tabRect(0);
            drag(bar, r.center(), bar->mapToGlobal(QPoint(r.center().x() + r.width() + 10, r.center().y())), 6);
        }
        report(bars > 0, qPrintable(tag + QStringLiteral("-reorder")), QStringLiteral("survived %1 tab bars").arg(bars));

        // Tear a tab off onto the canvas (the drag that first showed the crash).
        GigaFloatingPanelManager *m = w->gigaFloatingPanels();
        QDockWidget *hist = w->dockWidget(QStringLiteral("History"));
        int idx = -1;
        if (hist) hist->raise();
        pump(10);
        QTabBar *bar = hist ? tabBarWith(w, hist->windowTitle(), &idx) : nullptr;
        if (hist && !bar) { // not in a group in this layout: regroup
            w->applyGigaLayout();
            pump(20);
            hist->raise();
            pump(10);
            bar = tabBarWith(w, hist->windowTitle(), &idx);
        }
        if (m && bar) {
            drag(bar, bar->tabRect(idx).center(), w->centralWidget()->mapToGlobal(w->centralWidget()->rect().center()), 8);
            report(m->isFloating(hist), qPrintable(tag + QStringLiteral("-tearoff")), QStringLiteral("History floating"));
            m->dockAll(true);
            pump(10);
        } else {
            report(false, qPrintable(tag + QStringLiteral("-tearoff")), QStringLiteral("History tab not found"));
        }
    }
}

/// User report: float a panel, then switch tabs in a docked group on the
/// right -> the floating panel disappears.
void testTabSwitch(KisMainWindow *w)
{
    GigaFloatingPanelManager *m = w->gigaFloatingPanels();
    QWidget *canvas = w->centralWidget();
    GigaInterfaceModes *modes = w->findChild<GigaInterfaceModes *>();
    if (!m || !canvas || !modes) { report(false, "setup", QStringLiteral("no manager, canvas or modes")); return; }

    openTestDocument(w);

    for (int run = 0; run < 6; ++run) {
        const int mode = qEnvironmentVariableIsSet("GIGA_MODE") ? qEnvironmentVariableIntValue("GIGA_MODE") : (run < 3 ? int(GigaInterfaceModes::Studio) : int(GigaInterfaceModes::Photoshop));
        const int how = qEnvironmentVariableIsSet("GIGA_HOW") ? qEnvironmentVariableIntValue("GIGA_HOW") : run % 3; // 0 = API, 1 = drag its tab, 2 = drag its title bar
        modes->setMode(mode);
        pump(10);
        w->applyGigaLayout();
        pump(10);
        const QString tag = (mode == GigaInterfaceModes::Studio ? QStringLiteral("studio") : QStringLiteral("photoshop"))
            + (how == 0 ? QStringLiteral("-api") : how == 1 ? QStringLiteral("-tabdrag") : QStringLiteral("-titledrag"));

        QDockWidget *hist = w->dockWidget(QStringLiteral("History"));
        QDockWidget *layers = w->dockWidget(QStringLiteral("KisLayerBox"));
        if (!hist || !layers) { report(false, "setup", QStringLiteral("missing panels")); return; }
        const QRect mid(canvas->geometry().center() - QPoint(150, 150), QSize(300, 300));
        hist->raise();
        pump(10);
        const QPoint target = canvas->mapToGlobal(canvas->rect().center());
        if (how == 0) {
            m->floatPanel(hist, mid);
        } else if (how == 1) {
            int idx = -1;
            QTabBar *bar = tabBarWith(w, hist->windowTitle(), &idx);
            if (!bar) { report(false, qPrintable(tag), QStringLiteral("History has no tab")); continue; }
            drag(bar, bar->tabRect(idx).center(), target, 8);
        } else {
            drag(hist, QPoint(30, 8), target, 8);
        }
        pump(10);
        if (!m->isFloating(hist)) { report(false, qPrintable(tag), QStringLiteral("did not float")); m->dockAll(true); pump(10); continue; }
        GigaFloatingPanel *frame = qobject_cast<GigaFloatingPanel *>(hist->parentWidget());
        info(tag + QStringLiteral(": floating=%1 frameVisible=%2 frameHidden=%3 windowVisible=%4 histHidden=%5 frameGeom=%6")
                 .arg(m->isFloating(hist)).arg(frame && frame->isVisible()).arg(frame && frame->isHidden())
                 .arg(w->isVisible()).arg(hist->isHidden()).arg(frame ? rectText(frame->geometry()) : QString()));

        const QRect canvasBefore = canvas->geometry();
        // Every tab of every visible docked group: click it, check History.
        QList<QTabBar *> bars = w->findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly);
        int clicks = 0;
        bool ok = true;
        QString firstBad;
        for (QTabBar *bar : bars) {
            if (!bar->isVisible()) continue;
            for (int i = 0; i < bar->count(); ++i) {
                const QString t = bar->tabText(i);
                clickTab(bar, i);
                ++clicks;
                frame = qobject_cast<GigaFloatingPanel *>(hist->parentWidget());
                bool visible = frame && frame->isVisible() && !hist->isHidden();
                QString cover;
                if (visible) {
                    // What is really on top at the panel's centre?
                    QWidget *top = w->childAt(frame->geometry().center());
                    if (!top || !(top == frame || frame->isAncestorOf(top))) {
                        visible = false;
                        QWidget *topLevelChild = top;
                        while (topLevelChild && topLevelChild->parentWidget() != w) topLevelChild = topLevelChild->parentWidget();
                        cover = top ? QStringLiteral(" covered by %1 '%2' (window child %3 '%4' %5 hidden=%6)")
                                          .arg(QString::fromLatin1(top->metaObject()->className()), top->objectName(),
                                               topLevelChild ? QString::fromLatin1(topLevelChild->metaObject()->className()) : QString(),
                                               topLevelChild ? topLevelChild->objectName() : QString(),
                                               topLevelChild ? rectText(topLevelChild->geometry()) : QString())
                                          .arg(topLevelChild && topLevelChild->isHidden())
                                    : QStringLiteral(" nothing at centre");
                    }
                }
                if (!visible && ok) {
                    ok = false;
                    firstBad = QStringLiteral("after clicking tab '%1': frame=%2 frameVisible=%3 dockHidden=%4 parent=%5")
                                   .arg(t).arg(frame != nullptr).arg(frame && frame->isVisible()).arg(hist->isHidden())
                                   .arg(hist->parentWidget() ? QString::fromLatin1(hist->parentWidget()->metaObject()->className()) : QStringLiteral("none")) + cover;
                }
            }
        }
        info(tag + QStringLiteral(": canvas %1 -> %2").arg(rectText(canvasBefore), rectText(canvas->geometry())));
        report(ok && clicks > 0, qPrintable(tag + QStringLiteral("-click-tabs")),
               ok ? QStringLiteral("History still shown after %1 tab clicks").arg(clicks) : firstBad);
        m->dockAll(true);
        pump(10);
    }
}

/// What's on screen after a mode switch: docked panels (with their side),
/// toolbars, menu bar, status bar. Used to check that two switches into the
/// same mode give the same layout.
QString layoutSignature(KisMainWindow *w)
{
    QStringList docks;
    for (QDockWidget *dock : w->findChildren<QDockWidget *>()) {
        if (dock->isVisible()) docks << dock->objectName() + QLatin1Char('@') + QString::number(int(w->dockWidgetArea(dock)));
    }
    docks.sort();
    QStringList bars;
    for (QToolBar *bar : w->findChildren<QToolBar *>()) {
        if (bar->isVisible()) bars << bar->objectName() + QLatin1Char('@') + QString::number(int(w->toolBarArea(bar)));
    }
    bars.sort();
    return QStringLiteral("menu=%1 status=%2 bars=[%3] docks=[%4]")
        .arg(w->menuBar()->isVisible()).arg(w->statusBar()->isVisible())
        .arg(bars.join(QLatin1Char(' ')), docks.join(QLatin1Char(' ')));
}

/// A mode switch that rebuilds the menus (into/out of Studio) used to
/// re-apply the stale saved layout before restoring the mode's own one.
/// Checks: no extra restore, same layout each time a mode is entered,
/// Procreate still hides the bars. Prints how long each switch took.
void testModeSwitchRestore(KisMainWindow *w)
{
    GigaInterfaceModes *modes = w->findChild<GigaInterfaceModes *>();
    if (!modes) { report(false, "setup", QStringLiteral("no modes")); return; }
    openTestDocument(w);
    const char *names[] = {"studio", "photoshop", "procreate"};
    using M = GigaInterfaceModes;
    // Every ordered pair of modes appears at least once; the second half
    // repeats it so each mode is entered more than once.
    const QList<int> order = {M::Photoshop, M::Studio, M::Photoshop, M::Procreate, M::Studio, M::Procreate, M::Photoshop,
                              M::Studio, M::Photoshop, M::Procreate, M::Studio, M::Procreate, M::Photoshop};
    QHash<int, QString> seen;
    int current = -1;
    for (int mode : order) {
        const int before = w->gigaSavedLayoutRestores();
        QElapsedTimer timer;
        timer.start();
        modes->setMode(mode);
        const qint64 ms = timer.elapsed();
        pump(20);
        const QString tag = current < 0 ? QString::fromLatin1(names[mode])
                                        : QString::fromLatin1(names[current]) + QStringLiteral("->") + QString::fromLatin1(names[mode]);
        const int extra = w->gigaSavedLayoutRestores() - before;
        info(QStringLiteral("%1: %2 ms, saved-layout re-applies %3").arg(tag).arg(ms).arg(extra));
        report(extra == 0, qPrintable(tag + QStringLiteral("-no-extra-restore")), QStringLiteral("%1 extra").arg(extra));
        const QString sig = layoutSignature(w);
        if (current >= 0 && seen.contains(mode)) {
            report(seen.value(mode) == sig, qPrintable(tag + QStringLiteral("-same-layout")),
                   seen.value(mode) == sig ? sig : QStringLiteral("was %1 now %2").arg(seen.value(mode), sig));
        } else {
            info(tag + QStringLiteral(": ") + sig);
        }
        if (current >= 0) seen.insert(mode, sig); // (the very first state may predate the test)
        if (mode == M::Procreate) {
            report(!w->menuBar()->isVisible(), qPrintable(tag + QStringLiteral("-bars-hidden")), sig);
        }
        current = mode;
    }
}

/// User report: float a panel in Studio, cycle through the other modes,
/// back in Studio the panel is gone. Each mode should get its floating
/// panels back.
void testModeFloating(KisMainWindow *w)
{
    GigaInterfaceModes *modes = w->findChild<GigaInterfaceModes *>();
    GigaFloatingPanelManager *m = w->gigaFloatingPanels();
    if (!modes || !m) { report(false, "setup", QStringLiteral("no modes or manager")); return; }
    openTestDocument(w);
    const char *names[] = {"studio", "photoshop", "procreate"};
    using M = GigaInterfaceModes;
    QWidget *canvas = w->centralWidget();
    for (int start : {int(M::Studio), int(M::Photoshop)}) {
        const QString tag = QString::fromLatin1(names[start]);
        modes->setMode(start);
        pump(20);
        QDockWidget *hist = w->dockWidget(QStringLiteral("History"));
        if (!hist) { report(false, "setup", QStringLiteral("no History")); return; }
        const QRect mid(canvas->geometry().center() - QPoint(150, 150), QSize(300, 300));
        m->floatPanel(hist, mid);
        pump(10);
        info(tag + QStringLiteral(": History floating=%1 visible=%2").arg(m->isFloating(hist)).arg(hist->isVisible()));
        // Two full cycles, like pressing F9 six times.
        for (int i = 1; i <= 6; ++i) {
            const int mode = (start + i) % 3;
            modes->setMode(mode);
            pump(20);
            info(QStringLiteral("  -> %1: History floating=%2 visible=%3 key=%4")
                     .arg(QString::fromLatin1(names[mode])).arg(m->isFloating(hist)).arg(hist->isVisible())
                     .arg(KisConfig(true).readEntry<QString>(QStringLiteral("gigaModeFloatingPanels%1").arg(start), QString())));
        }
        report(m->isFloating(hist) && hist->isVisible(), qPrintable(tag + QStringLiteral("-floating-kept")),
               QStringLiteral("floating=%1 visible=%2").arg(m->isFloating(hist)).arg(hist->isVisible()));
        m->dockAll(true);
        pump(10);
    }
}

/// Reference Board: send to Board / back, arrange + undo, save/reload,
/// resample, and click-to-focus in the panel.
void testReferenceBoard(KisMainWindow *w)
{
    using namespace GigaReferenceBoard;
    KisDocument *doc = openTestDocument(w);
    KisView *view = w->activeView();
    KisCanvas2 *canvas = view ? view->canvasBase() : nullptr;
    if (!doc || !canvas) { report(false, "setup", QStringLiteral("no document/canvas")); return; }

    // Three references: a big photo-like one and two small ones.
    QList<KoShape *> shapes;
    const QList<QPair<QSize, QColor>> specs = {{QSize(3000, 2000), Qt::red}, {QSize(300, 600), Qt::green}, {QSize(500, 200), Qt::blue}};
    int z = 0;
    for (const auto &spec : specs) {
        QImage img(spec.first, QImage::Format_ARGB32);
        img.fill(spec.second);
        KisReferenceImage *ref = KisReferenceImage::fromQImage(*canvas->coordinatesConverter(), img);
        ref->setZIndex(z++);
        shapes.append(ref);
    }
    // Shrink the big one on the canvas (as if scaled down by hand).
    shapes[0]->setTransformation(QTransform::fromScale(0.1, 0.1));
    canvas->addCommand(KisReferenceImagesLayer::addReferenceImages(doc, shapes));
    pump(10);
    KisSharedPtr<KisReferenceImagesLayer> layer = doc->referenceImagesLayer();
    if (!layer || layer->referenceImages().size() != 3) { report(false, "setup", QStringLiteral("references not added")); return; }
    const QVector<KisReferenceImage *> refs = layer->referenceImages();

    QDockWidget *dock = w->dockWidget(QStringLiteral("GigaReferenceBoard"));
    report(dock != nullptr, "board-dock", dock ? QStringLiteral("registered") : QStringLiteral("missing"));
    if (!dock) return;
    dock->show();
    dock->raise();
    pump(10);
    QWidget *boardView = dock->findChild<QWidget *>(QStringLiteral("GigaReferenceBoardView"));
    if (!boardView) { report(false, "board-view", QStringLiteral("missing")); return; }

    // Send two to the Board: hidden on the canvas, placed on the Board.
    const QTransform canvasTransform1 = refs[1]->transformation();
    canvas->addCommand(sendToBoard({refs[1], refs[2]}, layer.data(), Settings::load()));
    pump(10);
    bool ok = refs[1]->isOnBoard() && refs[2]->isOnBoard() && !refs[1]->isVisible() && refs[0]->isVisible()
        && refs[1]->boardRect().isValid() && !refs[1]->boardRect().intersects(refs[2]->boardRect());
    report(ok, "send-to-board", QStringLiteral("board images: %1").arg(boardView->property("imageCount").toInt()));
    report(boardView->property("imageCount").toInt() == 2, "board-shows-images",
           QString::number(boardView->property("imageCount").toInt()));

    // Undo, redo.
    doc->undoStack()->undo();
    pump(5);
    ok = !refs[1]->isOnBoard() && refs[1]->isVisible() && boardView->property("imageCount").toInt() == 0;
    doc->undoStack()->redo();
    pump(5);
    ok = ok && refs[1]->isOnBoard();
    report(ok, "board-undo-redo", QString());

    // Click-to-focus on the Board.
    pump(20); // camera animation
    QPointF center;
    QMetaObject::invokeMethod(boardView, "imageWidgetCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, center), Q_ARG(int, 0));
    {
        const QPoint p = center.toPoint();
        QMouseEvent press(QEvent::MouseButtonPress, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(boardView, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(boardView, &release);
        pump(5);
    }
    report(boardView->property("focusedIndex").toInt() == 0, "board-click-focus",
           QStringLiteral("focused %1 (clicked %2,%3)").arg(boardView->property("focusedIndex").toInt()).arg(center.x()).arg(center.y()));
    {
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(boardView, &esc);
        pump(5);
    }
    report(boardView->property("focusedIndex").toInt() == -1, "board-esc-overview", QString());

    // Ctrl+click samples the colour (image 0 in z order is the green one).
    pump(20);
    QMetaObject::invokeMethod(boardView, "imageWidgetCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, center), Q_ARG(int, 0));
    {
        const QPoint p = center.toPoint();
        QMouseEvent press(QEvent::MouseButtonPress, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(boardView, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(boardView, &release);
        pump(5);
    }
    const QColor picked = canvas->resourceManager()->foregroundColor().toQColor();
    report(picked == QColor(Qt::green), "board-ctrl-click-colour", picked.name());

    // Drag an image on the Board and let go: it must stay where it was
    // dropped, with no glide from the old spot (user report: rubber-banding).
    {
        QElapsedTimer settle; // let the Esc fly-to finish (real time, not event rounds)
        settle.start();
        while (settle.elapsed() < 450) pump(1);
    }
    QMetaObject::invokeMethod(boardView, "imageWidgetCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, center), Q_ARG(int, 1));
    {
        const QPoint start = center.toPoint();
        const QPoint end = start + QPoint(40, 25);
        drag(boardView, start, boardView->mapToGlobal(end), 8);
        qreal worst = 0.0;
        QElapsedTimer watch;
        watch.start();
        while (watch.elapsed() < 300) { // longer than any animation
            QPointF now;
            QMetaObject::invokeMethod(boardView, "imageWidgetCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, now), Q_ARG(int, 1));
            worst = qMax(worst, QLineF(now, QPointF(end)).length());
            if (qEnvironmentVariableIsSet("GIGA_TRACE_DROP")) info(QStringLiteral("t=%1 at %2,%3 (drop %4,%5, start %6,%7)").arg(watch.elapsed()).arg(now.x()).arg(now.y()).arg(end.x()).arg(end.y()).arg(start.x()).arg(start.y()));
            pump(1);
        }
        report(worst < 1.5, "board-drop-no-rubberband", QStringLiteral("farthest from drop point: %1 px").arg(worst, 0, 'f', 1));
        doc->undoStack()->undo(); // put it back for the checks below
        pump(5);
    }

    // Send back to the canvas: exact same transform as before.
    canvas->addCommand(sendToCanvas({refs[1]}));
    pump(5);
    report(!refs[1]->isOnBoard() && refs[1]->isVisible() && qFuzzyCompare(refs[1]->transformation(), canvasTransform1),
           "send-to-canvas-same-place", QString());

    // Arrange on the canvas: bounding boxes no longer overlap; undo restores.
    QVector<KisReferenceImage *> onCanvas = canvasImages(layer.data());
    QVector<QTransform> before;
    for (KisReferenceImage *r : onCanvas) before.append(r->transformation());
    auto area = [](const QRectF &r) { return r.width() * r.height(); };
    const qreal areaBefore = area(onCanvas[0]->boundingRect()) + area(onCanvas[1]->boundingRect());
    Settings settings = Settings::load();
    settings.style = GigaReferenceLayout::Style::JustifiedRows;
    GeometryCommand *arrange = arrangeOnCanvas(onCanvas, settings, canvas, layer.data());
    canvas->addCommand(arrange);
    animateOnCanvas(arrange);
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 400) pump(1); // let the 200 ms animation finish
    // No overlap, and about the same total size as before (nothing jumps).
    const qreal areaAfter = area(onCanvas[0]->boundingRect()) + area(onCanvas[1]->boundingRect());
    ok = onCanvas.size() == 2 && !onCanvas[0]->boundingRect().adjusted(1, 1, -1, -1).intersects(onCanvas[1]->boundingRect())
        && qAbs(areaAfter / areaBefore - 1.0) < 0.15;
    report(ok, "arrange-canvas", QStringLiteral("area %3 -> %4: %1 / %2").arg(rectText(onCanvas[0]->boundingRect().toRect()), rectText(onCanvas[1]->boundingRect().toRect())).arg(areaBefore).arg(areaAfter));
    doc->undoStack()->undo();
    pump(5);
    ok = true;
    for (int i = 0; i < onCanvas.size(); ++i) ok = ok && qFuzzyCompare(onCanvas[i]->transformation(), before[i]);
    report(ok, "arrange-undo", QString());

    // Resample the big one: 3000 px shown at ~300 pt -> ~450 px (x1.5 headroom).
    const QString summary = resampleSummary({refs[0]});
    canvas->addCommand(resample({refs[0]}));
    pump(5);
    const QSize after = refs[0]->imagePixelSize();
    report(after.width() < 3000 && after.width() >= 300 && refs[0]->boundingRect().isValid(), "resample",
           QStringLiteral("%1 -> %2x%3").arg(summary).arg(after.width()).arg(after.height()));
    doc->undoStack()->undo();
    pump(5);
    report(refs[0]->imagePixelSize() == QSize(3000, 2000), "resample-undo", QString());

    // Save and reopen: Board state survives.
    const QRectF boardRect2 = refs[2]->boardRect();
    const QString path = QDir::home().filePath(QStringLiteral("refboard-test.kra"));
    ok = doc->exportDocumentSync(path, "application/x-krita");
    KisDocument *reopened = KisPart::instance()->createDocument();
    ok = ok && reopened->openPath(path, KisDocument::DontAddToRecent);
    int onBoard = 0;
    bool rectKept = false;
    if (ok && reopened->referenceImagesLayer()) {
        for (KisReferenceImage *r : reopened->referenceImagesLayer()->referenceImages()) {
            if (r->isOnBoard()) {
                ++onBoard;
                rectKept = rectKept || (qAbs(r->boardRect().x() - boardRect2.x()) < 0.01 && qAbs(r->boardRect().width() - boardRect2.width()) < 0.01);
                ok = ok && !r->isVisible();
            }
        }
    }
    report(ok && onBoard == 1 && rectKept, "save-reload", QStringLiteral("on board after reload: %1").arg(onBoard));
    delete reopened;
}

/// Layers panel bottom row: New, Duplicate, Delete side by side on the left.
void testLayerButtons(KisMainWindow *w)
{
    openTestDocument(w);
    QToolButton *add = w->findChild<QToolButton *>(QStringLiteral("bnAdd"));
    QToolButton *dup = w->findChild<QToolButton *>(QStringLiteral("bnDuplicate"));
    QToolButton *del = w->findChild<QToolButton *>(QStringLiteral("bnDelete"));
    if (!add || !dup || !del) {
        report(false, "layerbuttons-found", QStringLiteral("add %1 dup %2 del %3").arg(!!add).arg(!!dup).arg(!!del));
        return;
    }
    const bool visible = !add->isHidden() && !dup->isHidden() && !del->isHidden();
    const bool order = add->x() < dup->x() && dup->x() < del->x();
    const bool adjacent = dup->x() - (add->x() + add->width()) < 12 && del->x() - (dup->x() + dup->width()) < 12;
    report(visible && order && adjacent, "layerbuttons-order",
           QStringLiteral("x: new %1, duplicate %2, delete %3").arg(add->x()).arg(dup->x()).arg(del->x()));

    // (Clicking is not tested: in a headless run Krita never gets an active
    // layer, so Duplicate Layer stays disabled. The button just triggers
    // Krita's own "duplicatelayer" action.)
}

/// Lasso Fill tool: one lasso stroke fills the whole loop; with eraser mode
/// (E) on, the next lasso erases it again.
void testLassoFill(KisMainWindow *w)
{
    KisDocument *doc = openTestDocument(w);
    KisView *view = w->activeView();
    KisCanvas2 *canvas = view ? view->canvasBase() : nullptr;
    if (!doc || !canvas) { report(false, "setup", QStringLiteral("no document/canvas")); return; }
    report(KoToolRegistry::instance()->contains(QStringLiteral("GigaToolLassoFill")), "lassofill-registered", QString());
    QToolButton *button = w->findChild<QToolButton *>(QStringLiteral("GigaToolLassoFill"));
    report(button != nullptr, "lassofill-toolbox-button", button ? QStringLiteral("found") : QStringLiteral("missing"));

    KisImageSP image = doc->image();
    KisNodeSP layer = image->root()->lastChild();
    if (!layer || !layer->paintDevice()) { report(false, "setup", QStringLiteral("no paint layer")); return; }
    // In a test run the window never becomes active, so Krita never hands
    // the view to the view manager; do it by hand (no active layer otherwise).
    w->setActiveView(view);
    view->viewManager()->setCurrentView(view);
    pump(10);
    info(QStringLiteral("layer %1 (%2 children), view manager image ok: %3, view node: %4")
             .arg(layer->name()).arg(image->root()->childCount())
             .arg(view->viewManager()->image().data() == image.data())
             .arg(view->currentNode() ? view->currentNode()->name() : QStringLiteral("none")));
    view->viewManager()->nodeManager()->slotNonUiActivatedNode(layer);
    pump(5);
    if (!view->currentNode()) {
        // Still none (headless): set the node where the tools read it.
        view->setCurrentNode(layer);
        canvas->resourceManager()->setResource(KoCanvasResource::CurrentKritaNode, QVariant::fromValue(KisNodeWSP(layer)));
        pump(5);
    }
    const KoColorSpace *cs = layer->paintDevice()->colorSpace();
    canvas->resourceManager()->setForegroundColor(KoColor(Qt::red, cs));
    KoToolManager::instance()->switchToolRequested(QStringLiteral("GigaToolLassoFill"));
    pump(20);
    report(KoToolManager::instance()->activeToolId() == QLatin1String("GigaToolLassoFill"), "lassofill-active",
           KoToolManager::instance()->activeToolId());

    QWidget *cw = canvas->canvasWidget();
    const QPoint c = cw->rect().center();
    info(QStringLiteral("active node: %1").arg(view->viewManager()->activeNode() ? view->viewManager()->activeNode()->name() : QStringLiteral("none")));
    // Synthetic mouse events don't get through Krita's input manager in a
    // test run, so drive the tool directly, like the input manager does.
    KoToolBase *tool = KoToolManager::instance()->toolById(canvas, QStringLiteral("GigaToolLassoFill"));
    // The tool sets itself up when its options widget is built (normally by
    // the Tool Options panel); make sure that happened.
    if (tool) tool->optionWidgets();
    pump(5);
    auto lasso = [&]() {
        if (!tool) return;
        auto send = [&](int phase, const QPoint &p) {
            QMouseEvent e(phase == 0 ? QEvent::MouseButtonPress : phase == 1 ? QEvent::MouseMove : QEvent::MouseButtonRelease,
                          p, cw->mapToGlobal(p), Qt::LeftButton, phase == 2 ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            QSpontaneKeyEvent::setSpontaneous(&e);
            KoPointerEvent ev(&e, canvas->coordinatesConverter()->widgetToDocument(QPointF(p)));
            KisTool *kt = static_cast<KisTool *>(tool);
            if (phase == 0) kt->beginPrimaryAction(&ev);
            else if (phase == 1) kt->continuePrimaryAction(&ev);
            else kt->endPrimaryAction(&ev);
        };
        const int r = qMin(cw->width(), cw->height()) / 5;
        send(0, c + QPoint(r, 0));
        for (int i = 1; i <= 36; ++i) {
            const qreal a = i * 2 * M_PI / 36;
            send(1, c + QPoint(qRound(r * std::cos(a)), qRound(r * std::sin(a))));
            pump(1);
        }
        send(2, c + QPoint(r, 0));
        pump(5);
        image->waitForDone();
        pump(5);
    };
    const QPoint px = canvas->coordinatesConverter()->widgetToImage(QPointF(c)).toPoint();
    auto pixel = [&]() {
        QColor q;
        layer->paintDevice()->pixel(px.x(), px.y(), &q);
        return q;
    };
    const QColor before = pixel();
    lasso();
    const QColor filled = pixel();
    report(filled.red() > 200 && filled.green() < 50 && filled.alpha() == 255, "lassofill-fills",
           QStringLiteral("centre before %1, after %2 (undo: %3)").arg(before.name(QColor::HexArgb), filled.name(QColor::HexArgb),
                                                          doc->undoStack()->undoText()));

    KisAction *erase = view->viewManager()->actionManager()->actionByName(QStringLiteral("erase_action"));
    if (!erase) { report(false, "lassofill-erases", QStringLiteral("no erase_action")); return; }
    erase->trigger();
    pump(10);
    lasso();
    const QColor erased = pixel();
    report(erased.alpha() == 0, "lassofill-erases",
           QStringLiteral("centre after erase %1 (undo: %2)").arg(erased.name(QColor::HexArgb), doc->undoStack()->undoText()));
    erase->trigger();
    pump(10);

    // With custom blending options (blend mode Normal), E must still erase.
    QCheckBox *custom = nullptr;
    for (const QPointer<QWidget> &ow : tool->optionWidgets()) {
        if (!ow) continue;
        for (QCheckBox *cb : ow->findChildren<QCheckBox *>()) {
            if (cb->text().contains(QStringLiteral("custom blending"), Qt::CaseInsensitive)) custom = cb;
        }
    }
    if (!custom) { report(false, "lassofill-custom-blending", QStringLiteral("checkbox not found")); return; }
    lasso(); // fill again (eraser mode is off now)
    custom->setChecked(true);
    erase->trigger();
    pump(10);
    lasso();
    const QColor erasedCustom = pixel();
    report(erasedCustom.alpha() == 0, "lassofill-custom-blending-erases",
           QStringLiteral("centre %1 (undo: %2)").arg(erasedCustom.name(QColor::HexArgb), doc->undoStack()->undoText()));
    erase->trigger();
    custom->setChecked(false);
    pump(10);
}

/// Edit > Transform Last Stroke: paint a blue line and a red line across it,
/// lift the red one into the Transform tool, cancel (nothing changes), then
/// move it and apply; check pixels and the undo history.
void testLastStroke(KisMainWindow *w)
{
    KisDocument *doc = openTestDocument(w);
    KisView *view = w->activeView();
    KisCanvas2 *canvas = view ? view->canvasBase() : nullptr;
    if (!doc || !canvas) { report(false, "setup", QStringLiteral("no document/canvas")); return; }
    KisImageSP image = doc->image();
    KisNodeSP layer = image->root()->lastChild();
    if (!layer || !layer->paintDevice()) { report(false, "setup", QStringLiteral("no paint layer")); return; }
    w->setActiveView(view);
    view->viewManager()->setCurrentView(view);
    pump(10);
    view->viewManager()->nodeManager()->slotNonUiActivatedNode(layer);
    pump(5);
    if (!view->currentNode()) {
        view->setCurrentNode(layer);
        canvas->resourceManager()->setResource(KoCanvasResource::CurrentKritaNode, QVariant::fromValue(KisNodeWSP(layer)));
        pump(5);
    }
    const KoColorSpace *cs = layer->paintDevice()->colorSpace();

    // Let time pass with the event loop running. With @p idle, also wait
    // (at most 5 s) for the image to finish its work; never while a stroke
    // is still open (waitForDone would hang on it).
    auto pass = [&](int ms, bool idle) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) { pump(1); QThread::msleep(5); }
        if (idle) {
            t.restart();
            while (!image->isIdle() && t.elapsed() < 5000) { pump(1); QThread::msleep(5); }
        }
        pump(5);
    };
    auto wait = [&](int ms) { pass(ms, true); };

    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    pump(20);
    KoToolBase *brush = KoToolManager::instance()->toolById(canvas, QStringLiteral("KritaShape/KisToolBrush"));
    if (!brush) { report(false, "setup", QStringLiteral("no brush tool")); return; }
    brush->optionWidgets();
    pump(5);

    QWidget *cw = canvas->canvasWidget();
    const QPoint c = cw->rect().center();
    const int r = qMin(cw->width(), cw->height()) / 4;
    auto line = [&](const QPoint &a, const QPoint &b) {
        auto send = [&](int phase, const QPoint &p) {
            QMouseEvent e(phase == 0 ? QEvent::MouseButtonPress : phase == 1 ? QEvent::MouseMove : QEvent::MouseButtonRelease,
                          p, cw->mapToGlobal(p), Qt::LeftButton, phase == 2 ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            QSpontaneKeyEvent::setSpontaneous(&e);
            KoPointerEvent ev(&e, canvas->coordinatesConverter()->widgetToDocument(QPointF(p)));
            KisTool *kt = static_cast<KisTool *>(brush);
            if (phase == 0) kt->beginPrimaryAction(&ev);
            else if (phase == 1) kt->continuePrimaryAction(&ev);
            else kt->endPrimaryAction(&ev);
        };
        send(0, a);
        for (int i = 1; i <= 40; ++i) {
            send(1, a + (b - a) * i / 40);
            pass(8, false);
        }
        send(2, b);
        wait(600); // let a Smooth Curve tail land
    };
    auto px = [&](const QPoint &widgetPt) {
        return canvas->coordinatesConverter()->widgetToImage(QPointF(widgetPt)).toPoint();
    };
    auto pixel = [&](const QPoint &imagePt) {
        QColor q;
        layer->paintDevice()->pixel(imagePt.x(), imagePt.y(), &q);
        return q;
    };
    auto isRed = [](const QColor &q) { return q.alpha() > 200 && q.red() > 200 && q.blue() < 80; };
    auto isBlue = [](const QColor &q) { return q.alpha() > 200 && q.blue() > 200 && q.red() < 80; };
    auto isEmpty = [](const QColor &q) { return q.alpha() < 30 || (q.red() > 230 && q.green() > 230 && q.blue() > 230); };
    auto name = [](const QColor &q) { return q.name(QColor::HexArgb); };

    canvas->resourceManager()->setForegroundColor(KoColor(Qt::blue, cs));
    line(c + QPoint(-r, 0), c + QPoint(r, 0));                 // blue, horizontal
    canvas->resourceManager()->setForegroundColor(KoColor(Qt::red, cs));
    line(c + QPoint(0, -r), c + QPoint(0, r));                 // red, vertical, on top

    const QPoint cross = px(c);
    const QPoint onRed = px(c + QPoint(0, -r / 2));
    const QPoint onBlue = px(c + QPoint(-r / 2, 0));
    // Where the red line ends up after moving 60 image pixels to the right.
    const QPoint movedRed = onRed + QPoint(60, 0);
    KUndo2Stack *stack = doc->undoStack();
    const int countBefore = stack->count();
    const QString strokeName = stack->undoText();
    report(isRed(pixel(cross)) && isRed(pixel(onRed)) && isBlue(pixel(onBlue)), "laststroke-painted",
           QStringLiteral("cross %1, red %2, blue %3, undo '%4' (%5 entries)")
               .arg(name(pixel(cross)), name(pixel(onRed)), name(pixel(onBlue)), strokeName).arg(countBefore));

    KisAction *action = view->viewManager()->actionManager()->actionByName(QStringLiteral("giga_transform_last_stroke"));
    report(action != nullptr, "laststroke-action", action ? action->shortcut().toString() : QStringLiteral("missing"));
    if (!action) return;

    // Free transform for the test, so it can be moved by a number.
    KisConfig(false).writeEntry<int>("gigaLastStrokeTransformMode", 0);
    action->trigger();
    wait(400);
    KoToolBase *transform = KoToolManager::instance()->toolById(canvas, QStringLiteral("KisToolTransform"));
    report(KoToolManager::instance()->activeToolId() == QLatin1String("KisToolTransform"), "laststroke-opens-transform",
           KoToolManager::instance()->activeToolId());
    if (!transform) return;
    // While transforming, the history ends at the blue line (red one lifted).
    report(stack->index() == countBefore - 1, "laststroke-lifted",
           QStringLiteral("index %1 of %2, undo '%3'").arg(stack->index()).arg(stack->count()).arg(stack->undoText()));

    // Cancel: the red line is back exactly, history as before.
    static_cast<KisTool *>(transform)->requestStrokeCancellation();
    wait(400);
    report(isRed(pixel(cross)) && isRed(pixel(onRed)) && stack->count() == countBefore && stack->index() == countBefore,
           "laststroke-cancel-restores",
           QStringLiteral("cross %1, red %2, %3/%4 entries, undo '%5'")
               .arg(name(pixel(cross)), name(pixel(onRed))).arg(stack->index()).arg(stack->count()).arg(stack->undoText()));

    // Again, this time move it 60 px right and apply.
    action->trigger();
    wait(400);
    const double tx = transform->property("translateX").toDouble();
    transform->setProperty("translateX", tx + 60.0);
    QMetaObject::invokeMethod(transform, "requestImageRecalculation");
    wait(300);
    static_cast<KisTool *>(transform)->requestStrokeEnd();
    wait(800);
    report(isBlue(pixel(cross)) && isEmpty(pixel(onRed)) && isRed(pixel(movedRed)) && isBlue(pixel(onBlue)),
           "laststroke-apply-moves-only-the-stroke",
           QStringLiteral("cross %1, old red %2, moved red %3, blue %4")
               .arg(name(pixel(cross)), name(pixel(onRed)), name(pixel(movedRed)), name(pixel(onBlue))));
    report(stack->count() == countBefore + 1 && stack->undoText() == i18n("Transform Last Stroke"),
           "laststroke-history",
           QStringLiteral("%1 entries (was %2), top '%3'").arg(stack->count()).arg(countBefore).arg(stack->undoText()));

    // One undo: the red line as drawn. Two: gone.
    stack->undo();
    wait(300);
    report(isRed(pixel(cross)) && isRed(pixel(onRed)) && isEmpty(pixel(movedRed)), "laststroke-undo-shows-as-drawn",
           QStringLiteral("cross %1, red %2, moved spot %3, undo '%4'")
               .arg(name(pixel(cross)), name(pixel(onRed)), name(pixel(movedRed)), stack->undoText()));
    {
        // Krita drops a Ctrl+Z while the image is busy (e.g. a thumbnail idle
        // task that just started after the last wait); try again like a user would.
        const int before = stack->index();
        for (int tries = 0; tries < 5 && stack->index() == before; ++tries) {
            stack->undo();
            wait(300);
        }
    }
    report(isBlue(pixel(cross)) && isEmpty(pixel(onRed)), "laststroke-undo-twice-removes",
           QStringLiteral("cross %1, red spot %2").arg(name(pixel(cross)), name(pixel(onRed))));
    info(QStringLiteral("before redo: %1/%2 canRedo %3 idle %4").arg(stack->index()).arg(stack->count()).arg(stack->canRedo()).arg(image->isIdle()));
    // Retried like the undo above: Krita drops a redo while the image is busy.
    auto redoOnce = [&](int ms) {
        const int before = stack->index();
        for (int tries = 0; tries < 5 && stack->index() == before; ++tries) {
            stack->redo();
            wait(ms);
        }
    };
    redoOnce(200);
    info(QStringLiteral("after redo 1: %1/%2 cross %3 red %4").arg(stack->index()).arg(stack->count()).arg(name(pixel(cross)), name(pixel(onRed))));
    redoOnce(300);
    info(QStringLiteral("after redo 2: %1/%2").arg(stack->index()).arg(stack->count()));
    report(isBlue(pixel(cross)) && isRed(pixel(movedRed)), "laststroke-redo",
           QStringLiteral("cross %1, moved red %2").arg(name(pixel(cross)), name(pixel(movedRed))));

    // Default Warp mode, finished by switching to another tool (applies).
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    wait(200);
    line(c + QPoint(-r, r / 2), c + QPoint(r, r / 2));          // red, horizontal, lower
    const QPoint onLower = px(c + QPoint(r / 2, r / 2));
    const int countWarp = stack->count();
    KisConfig(false).writeEntry<int>("gigaLastStrokeTransformMode", 1);
    action->trigger();
    wait(400);
    int modeNow = -1;
    QMetaObject::invokeMethod(transform, "gigaCurrentMode", Q_RETURN_ARG(int, modeNow));
    report(KoToolManager::instance()->activeToolId() == QLatin1String("KisToolTransform") && modeNow == 1,
           "laststroke-opens-in-warp", QStringLiteral("tool %1, mode %2").arg(KoToolManager::instance()->activeToolId()).arg(modeNow));
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    wait(800);
    report(isRed(pixel(onLower)) && stack->count() == countWarp && stack->index() == countWarp,
           "laststroke-tool-switch-keeps-stroke",
           QStringLiteral("lower line %1, %2/%3 entries (was %4), undo '%5'")
               .arg(name(pixel(onLower))).arg(stack->index()).arg(stack->count()).arg(countWarp).arg(stack->undoText()));

    // Unchanged, so it can be opened again; cancel this time.
    action->trigger();
    wait(400);
    report(KoToolManager::instance()->activeToolId() == QLatin1String("KisToolTransform")
               && stack->index() == countWarp - 1,
           "laststroke-reopens-unchanged-stroke",
           QStringLiteral("tool %1, %2/%3 entries").arg(KoToolManager::instance()->activeToolId()).arg(stack->index()).arg(stack->count()));
    static_cast<KisTool *>(transform)->requestStrokeCancellation();
    wait(400);
    report(isRed(pixel(onLower)) && stack->count() == countWarp && stack->index() == countWarp,
           "laststroke-reopen-cancel-restores",
           QStringLiteral("lower line %1, %2/%3 entries").arg(name(pixel(onLower))).arg(stack->index()).arg(stack->count()));

    // After any other action (here: a new layer), it must refuse and change nothing.
    view->viewManager()->nodeManager()->createNode(QStringLiteral("KisPaintLayer"));
    wait(300);
    const int countOther = stack->count();
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    wait(200);
    action->trigger();
    wait(300);
    report(KoToolManager::instance()->activeToolId() != QLatin1String("KisToolTransform")
               && stack->count() == countOther && stack->index() == countOther && isRed(pixel(onLower)),
           "laststroke-refuses-after-other-action",
           QStringLiteral("tool %1, %2/%3 entries").arg(KoToolManager::instance()->activeToolId()).arg(stack->index()).arg(stack->count()));
}

/// Transform Last Stroke, Points mode: drag a dot of the last stroke, the
/// brush repaints it along the bent path; Ctrl+Z steps the dots back; apply,
/// cancel and switching to Pixels and back (feature/stroke-points).
void testStrokePoints(KisMainWindow *w)
{
    KisDocument *doc = openTestDocument(w);
    KisView *view = w->activeView();
    KisCanvas2 *canvas = view ? view->canvasBase() : nullptr;
    if (!doc || !canvas) { report(false, "setup", QStringLiteral("no document/canvas")); return; }
    KisImageSP image = doc->image();
    KisNodeSP layer = image->root()->lastChild();
    if (!layer || !layer->paintDevice()) { report(false, "setup", QStringLiteral("no paint layer")); return; }
    w->setActiveView(view);
    view->viewManager()->setCurrentView(view);
    pump(10);
    view->viewManager()->nodeManager()->slotNonUiActivatedNode(layer);
    pump(5);
    if (!view->currentNode()) {
        view->setCurrentNode(layer);
        canvas->resourceManager()->setResource(KoCanvasResource::CurrentKritaNode, QVariant::fromValue(KisNodeWSP(layer)));
        pump(5);
    }
    const KoColorSpace *cs = layer->paintDevice()->colorSpace();
    auto pass = [&](int ms, bool idle) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) { pump(1); QThread::msleep(5); }
        if (idle) {
            t.restart();
            while (!image->isIdle() && t.elapsed() < 5000) { pump(1); QThread::msleep(5); }
        }
        pump(5);
    };
    auto wait = [&](int ms) { pass(ms, true); };
    const QString pointsId = QStringLiteral("GigaToolStrokePoints");
    auto activeTool = []() { return KoToolManager::instance()->activeToolId(); };

    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    pump(20);
    KoToolBase *brush = KoToolManager::instance()->toolById(canvas, QStringLiteral("KritaShape/KisToolBrush"));
    KoToolBase *points = KoToolManager::instance()->toolById(canvas, pointsId);
    KoToolBase *transform = KoToolManager::instance()->toolById(canvas, QStringLiteral("KisToolTransform"));
    report(brush && points && transform, "strokepoints-tools", QStringLiteral("brush %1, points %2, transform %3")
           .arg(!!brush).arg(!!points).arg(!!transform));
    if (!brush || !points || !transform) return;
    brush->optionWidgets();
    pump(5);

    QWidget *cw = canvas->canvasWidget();
    const KisCoordinatesConverter *conv = canvas->coordinatesConverter();
    // Send press / moves / release to a tool, at widget points.
    auto drive = [&](KoToolBase *tool, const QPoint &a, const QPoint &b, int steps) {
        auto send = [&](int phase, const QPoint &p) {
            QMouseEvent e(phase == 0 ? QEvent::MouseButtonPress : phase == 1 ? QEvent::MouseMove : QEvent::MouseButtonRelease,
                          p, cw->mapToGlobal(p), Qt::LeftButton, phase == 2 ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            QSpontaneKeyEvent::setSpontaneous(&e);
            KoPointerEvent ev(&e, conv->widgetToDocument(QPointF(p)));
            KisTool *kt = static_cast<KisTool *>(tool);
            if (phase == 0) kt->beginPrimaryAction(&ev);
            else if (phase == 1) kt->continuePrimaryAction(&ev);
            else kt->endPrimaryAction(&ev);
        };
        send(0, a);
        for (int i = 1; i <= steps; ++i) {
            send(1, a + (b - a) * i / steps);
            pass(8, false);
        }
        send(2, b);
    };
    const QPoint c = cw->rect().center();
    const int r = qMin(cw->width(), cw->height()) / 4;
    auto line = [&](const QPoint &a, const QPoint &b) {
        drive(brush, a, b, 40);
        wait(600);
    };
    auto toWidget = [&](const QPointF &imagePt) { return conv->imageToWidget(imagePt).toPoint(); };
    auto pixel = [&](const QPoint &imagePt) {
        QColor q;
        layer->paintDevice()->pixel(imagePt.x(), imagePt.y(), &q);
        return q;
    };
    auto isRed = [](const QColor &q) { return q.alpha() > 200 && q.red() > 200 && q.blue() < 80; };
    auto isBlue = [](const QColor &q) { return q.alpha() > 200 && q.blue() > 200 && q.red() < 80; };
    auto isEmpty = [](const QColor &q) { return q.alpha() < 30 || (q.red() > 230 && q.green() > 230 && q.blue() > 230); };
    auto name = [](const QColor &q) { return q.name(QColor::HexArgb); };
    auto vertexCount = [&]() {
        int n = 0;
        QMetaObject::invokeMethod(points, "gigaVertexCount", Q_RETURN_ARG(int, n));
        return n;
    };
    auto vertexPos = [&](int i) {
        QPointF p;
        QMetaObject::invokeMethod(points, "gigaVertexImagePos", Q_RETURN_ARG(QPointF, p), Q_ARG(int, i));
        return p;
    };

    KisAction *action = view->viewManager()->actionManager()->actionByName(QStringLiteral("giga_transform_last_stroke"));
    if (!action) { report(false, "strokepoints-action", QStringLiteral("missing")); return; }
    KUndo2Stack *stack = doc->undoStack();

    // Blue vertical, then red horizontal across it (the one we edit).
    canvas->resourceManager()->setForegroundColor(KoColor(Qt::blue, cs));
    line(c + QPoint(0, -r), c + QPoint(0, r));
    canvas->resourceManager()->setForegroundColor(KoColor(Qt::red, cs));
    line(c + QPoint(-r, 0), c + QPoint(r, 0));
    const int countBefore = stack->count();
    const QPoint cross = conv->widgetToImage(QPointF(c)).toPoint();
    const QPoint redEnd = conv->widgetToImage(QPointF(c + QPoint(-r + r / 8, 0))).toPoint();

    KisConfig(false).writeEntry<int>("gigaLastStrokeEditMode", 1);
    KisConfig(false).writeEntry<int>("gigaStrokePointSpacing", 40);
    action->trigger();
    wait(400);
    const int n = vertexCount();
    report(activeTool() == pointsId && n >= 3, "strokepoints-opens-with-dots",
           QStringLiteral("tool %1, %2 dots").arg(activeTool()).arg(n));
    report(stack->index() == countBefore - 1 && isRed(pixel(cross)) && isRed(pixel(redEnd)), "strokepoints-lifted-but-shown",
           QStringLiteral("index %1 of %2, cross %3, red end %4").arg(stack->index()).arg(stack->count())
               .arg(name(pixel(cross)), name(pixel(redEnd))));
    if (activeTool() != pointsId || n < 3) return;

    // Drag the dot nearest the middle 60 image px down.
    int mid = 0;
    for (int i = 0; i < n; ++i) {
        if (QLineF(vertexPos(i), QPointF(cross)).length() < QLineF(vertexPos(mid), QPointF(cross)).length()) mid = i;
    }
    const QPointF from = vertexPos(mid);
    const QPoint moved = (from + QPointF(0, 60)).toPoint();
    drive(points, toWidget(from), toWidget(from + QPointF(0, 60)), 12);
    wait(800);
    report(isRed(pixel(moved)) && isBlue(pixel(cross)) && isRed(pixel(redEnd)), "strokepoints-drag-repaints-bent",
           QStringLiteral("moved spot %1, cross %2 (blue = uncovered), red end %3")
               .arg(name(pixel(moved)), name(pixel(cross)), name(pixel(redEnd))));
    report(stack->index() == countBefore - 1, "strokepoints-no-history-while-editing",
           QStringLiteral("index %1 (want %2)").arg(stack->index()).arg(countBefore - 1));

    // Ctrl+Z steps the dot back (not the document's history), Ctrl+Shift+Z again.
    stack->undo();
    wait(800);
    report(activeTool() == pointsId && isRed(pixel(cross)) && isEmpty(pixel(moved)) && stack->index() == countBefore - 1,
           "strokepoints-undo-steps-dots", QStringLiteral("tool %1, cross %2, moved spot %3, index %4")
               .arg(activeTool(), name(pixel(cross)), name(pixel(moved))).arg(stack->index()));
    stack->redo();
    wait(800);
    report(isRed(pixel(moved)) && isBlue(pixel(cross)), "strokepoints-redo-steps-dots",
           QStringLiteral("moved spot %1, cross %2").arg(name(pixel(moved)), name(pixel(cross))));

    // Apply (Enter): two history entries, back to the brush.
    static_cast<KisTool *>(points)->requestStrokeEnd();
    wait(800);
    report(isRed(pixel(moved)) && isBlue(pixel(cross)) && stack->count() == countBefore + 1
               && stack->undoText() == i18n("Edit Stroke Points") && activeTool() != pointsId,
           "strokepoints-apply", QStringLiteral("moved %1, cross %2, %3 entries (was %4), top '%5', tool %6")
               .arg(name(pixel(moved)), name(pixel(cross))).arg(stack->count()).arg(countBefore)
               .arg(stack->undoText(), activeTool()));
    stack->undo();
    wait(400);
    report(isRed(pixel(cross)) && isEmpty(pixel(moved)), "strokepoints-undo-shows-as-drawn",
           QStringLiteral("cross %1, moved spot %2").arg(name(pixel(cross)), name(pixel(moved))));
    {
        const int before = stack->index();
        for (int tries = 0; tries < 5 && stack->index() == before; ++tries) {
            stack->undo(); // retried: Krita drops an undo while the image is busy
            wait(400);
        }
    }
    report(isBlue(pixel(cross)) && isEmpty(pixel(redEnd)), "strokepoints-undo-twice-removes",
           QStringLiteral("cross %1, red end %2").arg(name(pixel(cross)), name(pixel(redEnd))));

    // Esc: a new red line, bend it, cancel: as drawn, history unchanged.
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    wait(200);
    line(c + QPoint(-r, 0), c + QPoint(r, 0));
    const int countEsc = stack->count();
    action->trigger();
    wait(400);
    const QPointF from2 = vertexPos(qMax(1, vertexCount() / 2));
    drive(points, toWidget(from2), toWidget(from2 + QPointF(0, 60)), 12);
    wait(800);
    static_cast<KisTool *>(points)->requestStrokeCancellation();
    wait(600);
    report(isRed(pixel(from2.toPoint())) && isEmpty(pixel((from2 + QPointF(0, 60)).toPoint()))
               && stack->count() == countEsc && stack->index() == countEsc && activeTool() != pointsId,
           "strokepoints-cancel-restores", QStringLiteral("old spot %1, bent spot %2, %3/%4 entries (was %5), tool %6")
               .arg(name(pixel(from2.toPoint())), name(pixel((from2 + QPointF(0, 60)).toPoint())))
               .arg(stack->index()).arg(stack->count()).arg(countEsc).arg(activeTool()));

    // Points -> Pixels with the shortcut: the bent stroke goes to the Transform tool.
    action->trigger();
    wait(400);
    const QPointF from3 = vertexPos(qMax(1, vertexCount() / 2));
    drive(points, toWidget(from3), toWidget(from3 + QPointF(0, 60)), 12);
    wait(800);
    action->trigger();
    wait(600);
    report(activeTool() == QLatin1String("KisToolTransform"), "strokepoints-switch-to-pixels",
           QStringLiteral("tool %1").arg(activeTool()));
    static_cast<KisTool *>(transform)->requestStrokeEnd();
    wait(800);
    report(isRed(pixel((from3 + QPointF(0, 60)).toPoint())) && stack->count() == countEsc + 1,
           "strokepoints-pixels-applies-bent", QStringLiteral("bent spot %1, %2 entries (was %3), top '%4'")
               .arg(name(pixel((from3 + QPointF(0, 60)).toPoint()))).arg(stack->count()).arg(countEsc).arg(stack->undoText()));

    // Pixels -> Points: open in Pixels, press again: dots.
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    wait(200);
    line(c + QPoint(-r, r / 2), c + QPoint(r, r / 2));
    const int countSwitch = stack->count();
    KisConfig(false).writeEntry<int>("gigaLastStrokeEditMode", 0);
    action->trigger();
    wait(400);
    const bool openedPixels = activeTool() == QLatin1String("KisToolTransform");
    action->trigger();
    wait(800);
    report(openedPixels && activeTool() == pointsId && vertexCount() >= 2, "strokepoints-switch-to-points",
           QStringLiteral("opened in %1, now %2, %3 dots").arg(openedPixels ? QStringLiteral("Transform") : QStringLiteral("?"))
               .arg(activeTool()).arg(vertexCount()));
    static_cast<KisTool *>(points)->requestStrokeCancellation();
    wait(600);
    report(stack->count() == countSwitch && stack->index() == countSwitch, "strokepoints-switch-cancel-clean",
           QStringLiteral("%1/%2 entries (was %3)").arg(stack->index()).arg(stack->count()).arg(countSwitch));

    // Proportional editing: a new horizontal line higher up, drag its middle
    // dot 60 px down with a radius of a quarter of the line (Smooth falloff).
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    wait(200);
    line(c + QPoint(-r, -r / 2), c + QPoint(r, -r / 2));
    KisConfig(false).writeEntry<int>("gigaLastStrokeEditMode", 1);
    action->trigger();
    wait(400);
    const qreal scale = QLineF(conv->imageToWidget(QPointF(0, 0)), conv->imageToWidget(QPointF(100, 0))).length() / 100.0;
    const int radiusScreen = qMax(20, r / 2);           // = r/2 screen px = a quarter of the line
    const qreal radiusImage = radiusScreen / scale;
    QMetaObject::invokeMethod(points, "gigaSetProportional", Q_ARG(bool, true), Q_ARG(int, radiusScreen), Q_ARG(int, 0));
    const QPointF midTop = conv->widgetToImage(QPointF(c + QPoint(0, -r / 2)));
    int pmid = 0;
    for (int i = 0; i < vertexCount(); ++i) {
        if (QLineF(vertexPos(i), midTop).length() < QLineF(vertexPos(pmid), midTop).length()) pmid = i;
    }
    const QPointF pfrom = vertexPos(pmid);
    drive(points, toWidget(pfrom), toWidget(pfrom + QPointF(0, 60)), 12);
    wait(800);
    // Half the radius away along the line, Smooth gives weight 0.5 -> 30 px down.
    const QPoint half = (pfrom + QPointF(radiusImage * 0.5, 30)).toPoint();
    const QPoint halfOld = (pfrom + QPointF(radiusImage * 0.5, 0)).toPoint();
    const QPoint beyond = (pfrom + QPointF(radiusImage * 1.3, 0)).toPoint();
    report(isRed(pixel((pfrom + QPointF(0, 60)).toPoint())) && isRed(pixel(half)) && isEmpty(pixel(halfOld)) && isRed(pixel(beyond)),
           "strokepoints-proportional-falloff",
           QStringLiteral("centre %1, half-radius bent %2 (old place %3), beyond radius %4; radius %5 image px")
               .arg(name(pixel((pfrom + QPointF(0, 60)).toPoint())), name(pixel(half)), name(pixel(halfOld)), name(pixel(beyond)))
               .arg(radiusImage, 0, 'f', 1));

    // Mouse wheel during a drag: radius grows 15%, no zoom.
    const qreal zoomBefore = scale;
    {
        const QPointF at = vertexPos(pmid);
        const QPoint a = toWidget(at);
        const QPoint b = toWidget(at + QPointF(0, 20));
        auto send = [&](int phase, const QPoint &p) {
            QMouseEvent e(phase == 0 ? QEvent::MouseButtonPress : phase == 1 ? QEvent::MouseMove : QEvent::MouseButtonRelease,
                          p, cw->mapToGlobal(p), Qt::LeftButton, phase == 2 ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            KoPointerEvent ev(&e, conv->widgetToDocument(QPointF(p)));
            KisTool *kt = static_cast<KisTool *>(points);
            if (phase == 0) kt->beginPrimaryAction(&ev);
            else if (phase == 1) kt->continuePrimaryAction(&ev);
            else kt->endPrimaryAction(&ev);
        };
        send(0, a);
        send(1, (a + b) / 2);
        QWheelEvent wheel(QPointF(b), QPointF(cw->mapToGlobal(b)), QPoint(), QPoint(0, 120), Qt::LeftButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(cw, &wheel);
        send(1, b);
        send(2, b);
    }
    wait(800);
    int radiusNow = 0;
    QMetaObject::invokeMethod(points, "gigaRadius", Q_RETURN_ARG(int, radiusNow));
    const qreal zoomAfter = QLineF(conv->imageToWidget(QPointF(0, 0)), conv->imageToWidget(QPointF(100, 0))).length() / 100.0;
    report(radiusNow == qRound(radiusScreen * 1.15) && qAbs(zoomAfter - zoomBefore) < 1e-6, "strokepoints-wheel-radius",
           QStringLiteral("radius %1 -> %2 (want %3), zoom %4 -> %5").arg(radiusScreen).arg(radiusNow)
               .arg(qRound(radiusScreen * 1.15)).arg(zoomBefore).arg(zoomAfter));

    // Ctrl+Z twice: both proportional moves gone, the line straight again.
    stack->undo();
    wait(600);
    stack->undo();
    wait(800);
    report(isRed(pixel(halfOld)) && isEmpty(pixel(half)) && activeTool() == pointsId, "strokepoints-proportional-undo",
           QStringLiteral("half-radius old place %1, bent place %2, tool %3").arg(name(pixel(halfOld)), name(pixel(half)), activeTool()));
    QMetaObject::invokeMethod(points, "gigaSetProportional", Q_ARG(bool, false), Q_ARG(int, 120), Q_ARG(int, 0));

    // Live: while the dot is still held, the stroke has already moved.
    {
        const QPointF at = vertexPos(pmid);
        const QPoint a = toWidget(at);
        const QPoint b = toWidget(at + QPointF(0, 40));
        auto send = [&](int phase, const QPoint &p) {
            QMouseEvent e(phase == 0 ? QEvent::MouseButtonPress : phase == 1 ? QEvent::MouseMove : QEvent::MouseButtonRelease,
                          p, cw->mapToGlobal(p), Qt::LeftButton, phase == 2 ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            KoPointerEvent ev(&e, conv->widgetToDocument(QPointF(p)));
            KisTool *kt = static_cast<KisTool *>(points);
            if (phase == 0) kt->beginPrimaryAction(&ev);
            else if (phase == 1) kt->continuePrimaryAction(&ev);
            else kt->endPrimaryAction(&ev);
        };
        send(0, a);
        for (int i = 1; i <= 10; ++i) {
            send(1, a + (b - a) * i / 10);
            pass(15, false);
        }
        pass(800, false); // button still down: no release yet
        const QColor held = pixel((at + QPointF(0, 40)).toPoint());
        const QColor oldPlace = pixel(at.toPoint());
        send(2, b);
        wait(600);
        report(isRed(held) && isEmpty(oldPlace), "strokepoints-live-while-dragging",
               QStringLiteral("while held: new place %1, old place %2").arg(name(held), name(oldPlace)));
    }
    static_cast<KisTool *>(points)->requestStrokeCancellation();
    wait(600);
    KisConfig(false).writeEntry<int>("gigaLastStrokeEditMode", 0);
}

bool runNamed(const QString &name, KisMainWindow *w)
{
    const QList<QPair<QString, std::function<void(KisMainWindow *)>>> tests = {
        {QStringLiteral("panels"), testPanels},
        {QStringLiteral("tabswitch"), testTabSwitch},
        {QStringLiteral("panelmove"), testPanelMove},
        {QStringLiteral("panelsnap"), testPanelSnap},
        {QStringLiteral("modecrash"), testModeSwitchTabDrag},
        {QStringLiteral("modeswitch"), testModeSwitchRestore},
        {QStringLiteral("modefloat"), testModeFloating},
        {QStringLiteral("autosave"), testAutosaveName},
        {QStringLiteral("refboard"), testReferenceBoard},
        {QStringLiteral("layerbuttons"), testLayerButtons},
        {QStringLiteral("lassofill"), testLassoFill},
        {QStringLiteral("laststroke"), testLastStroke},
        {QStringLiteral("strokepoints"), testStrokePoints},
    };
    bool found = false;
    for (const auto &test : tests) {
        if (name == test.first || name == QLatin1String("all")) {
            info(QStringLiteral("running ") + test.first);
            test.second(w);
            found = true;
        }
    }
    return found;
}
} // namespace

void GigaSelfTest::maybeRun(KisMainWindow *window)
{
    static bool started = false;
    const QString name = qEnvironmentVariable("GIGA_SELFTEST");
    if (started || name.isEmpty() || !window) return;
    started = true;

    const int delay = qEnvironmentVariableIntValue("GIGA_SELFTEST_DELAY") > 0
        ? qEnvironmentVariableIntValue("GIGA_SELFTEST_DELAY") : 5000;
    QPointer<KisMainWindow> w = window;
    QTimer::singleShot(delay, qApp, [w, name]() {
        if (!w) return;
        if (!runNamed(name, w)) report(false, "setup", QStringLiteral("unknown test ") + name);
        fprintf(stderr, "GIGA_SELFTEST DONE %d failed\n", s_failures);
        fflush(stderr);
        // Skip Krita's normal shutdown (save prompts, session saving): this is
        // a throw-away config anyway.
        _Exit(s_failures);
    });
}
