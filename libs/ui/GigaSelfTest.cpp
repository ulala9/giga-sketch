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
#include <GigaBranding.h>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QTimer>

#include <cstdio>
#include <functional>

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

bool runNamed(const QString &name, KisMainWindow *w)
{
    const QList<QPair<QString, std::function<void(KisMainWindow *)>>> tests = {
        {QStringLiteral("panels"), testPanels},
        {QStringLiteral("tabswitch"), testTabSwitch},
        {QStringLiteral("panelmove"), testPanelMove},
        {QStringLiteral("modecrash"), testModeSwitchTabDrag},
        {QStringLiteral("modeswitch"), testModeSwitchRestore},
        {QStringLiteral("modefloat"), testModeFloating},
        {QStringLiteral("autosave"), testAutosaveName},
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
