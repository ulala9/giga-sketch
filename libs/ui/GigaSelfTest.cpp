/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaSelfTest.h"

#include "GigaFloatingPanel.h"
#include "GigaFloatingPanelManager.h"
#include "KisMainWindow.h"

#include <QApplication>
#include <QDockWidget>
#include <QMouseEvent>
#include <QPointer>
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
    for (int i = 1; i <= steps; ++i) {
        const QPoint g = g0 + (globalEnd - g0) * i / steps;
        QMouseEvent move(QEvent::MouseMove, target->mapFromGlobal(g), g, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &move);
    }
    if (beforeRelease) beforeRelease();
    QMouseEvent release(QEvent::MouseButtonRelease, target->mapFromGlobal(globalEnd), globalEnd, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &release);
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

bool runNamed(const QString &name, KisMainWindow *w)
{
    const QList<QPair<QString, std::function<void(KisMainWindow *)>>> tests = {
        {QStringLiteral("panels"), testPanels},
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
