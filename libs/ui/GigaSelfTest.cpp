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
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>

#include <QApplication>
#include <QDockWidget>
#include <QMouseEvent>
#include <QPointer>
#include <QTabBar>
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
    for (int i = 0; i < rounds; ++i) qApp->processEvents();
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

/// User report: float a panel, then switch tabs in a docked group on the
/// right -> the floating panel disappears.
void testTabSwitch(KisMainWindow *w)
{
    GigaFloatingPanelManager *m = w->gigaFloatingPanels();
    QWidget *canvas = w->centralWidget();
    GigaInterfaceModes *modes = w->findChild<GigaInterfaceModes *>();
    if (!m || !canvas || !modes) { report(false, "setup", QStringLiteral("no manager, canvas or modes")); return; }

    // Leave the Welcome page (it hides every panel): open a document.
    KisDocument *doc = KisPart::instance()->createDocument();
    KisPart::instance()->addDocument(doc, false);
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    doc->newImage(QStringLiteral("selftest"), 800, 600, cs, KoColor(Qt::white, cs), KisConfig::RASTER_LAYER, 1, QString(), 1.0);
    w->showDocument(doc);
    pump(20);

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

bool runNamed(const QString &name, KisMainWindow *w)
{
    const QList<QPair<QString, std::function<void(KisMainWindow *)>>> tests = {
        {QStringLiteral("panels"), testPanels},
        {QStringLiteral("tabswitch"), testTabSwitch},
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
