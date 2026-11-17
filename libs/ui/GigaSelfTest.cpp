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
#include "GigaNote.h"
#include "GigaNoteActions.h"
#include "GigaNoteEditor.h"
#include <QClipboard>
#include <QMenu>
#include <KoSelection.h>
#include <KoShapeManager.h>
#include "GigaZoomOption.h"
#include "canvas/kis_canvas_controller.h"
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
#include <kis_action_registry.h>
#include <KConfigGroup>
#include <KSharedConfig>
#include <KoPointerEvent.h>
#include <kis_tool.h>
#include <kis_tool_paint.h>
#include "kis_grid_config.h"
#include "kis_grid_manager.h"
#include "kis_canvas_decoration.h"
#include "kis_zoom_manager.h"
#include "kis_config_notifier.h"
#include <GigaBranding.h>
#include <kis_icon_utils.h>
#include "GigaMotion.h"
#include "GigaAccent.h"
#include "GigaBlenderBridge.h"
#include "kis_file_layer.h"
#include "kis_transform_mask.h"
#include "kis_transform_mask_params_interface.h"
#include "kis_node_commands_adapter.h"
#include <kis_layer_properties_icons.h>
#include "kis_layer_utils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <kundo2stack.h>
#include <klocalizedstring.h>
#include <KoCanvasResourceProvider.h>
#include <KoColor.h>
#include <KoProperties.h>
#include <KoColorSpaceRegistry.h>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDropEvent>
#include <QMimeData>
#include <QLabel>
#include <QBuffer>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QThread>

#include <cmath>
#include <algorithm>
#include <climits>
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

// Blender bridge (feature/blender-bridge): a fake Blender add-on talks to the
// bridge over its socket; a "send" frame adds one File Layer above the active
// layer (active layer unchanged), a second one only reloads it, a "refresh"
// frame never adds layers, and requestRefresh() asks Blender for a picture.
void testBlenderBridge(KisMainWindow *w)
{
    GigaBlenderBridge *bridge = GigaBlenderBridge::instance();
    report(bridge != nullptr, "blender-bridge-started", bridge ? bridge->socketPath() : QString());
    if (!bridge) return;
    KisDocument *doc = openTestDocument(w);
    KisImageSP image = doc->image();
    KisViewManager *vm = w->viewManager();

    const QString folder = QDir::home().filePath(QStringLiteral("blender"));
    QDir().mkpath(folder);
    const QString png = QDir(folder).filePath(QStringLiteral("test-Camera.png"));
    const QString png2 = QDir(folder).filePath(QStringLiteral("test-Other.png"));
    QImage pic(image->width(), image->height(), QImage::Format_ARGB32);
    pic.fill(QColor(255, 0, 0));
    pic.save(png);
    pic.save(png2);

    QLocalSocket client;
    client.connectToServer(bridge->socketPath());
    QElapsedTimer t; t.start();
    while ((client.state() != QLocalSocket::ConnectedState || bridge->clientCount() < 1) && t.elapsed() < 3000) pump(1);
    report(bridge->clientCount() == 1, "blender-connect", QString::number(bridge->clientCount()));

    auto sendMsg = [&](const QJsonObject &o) {
        client.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
        client.flush();
    };
    auto readMsg = [&]() -> QJsonObject {
        QElapsedTimer rt; rt.start();
        while (!client.canReadLine() && rt.elapsed() < 3000) { pump(1); client.waitForReadyRead(10); }
        if (!client.canReadLine()) return QJsonObject();
        return QJsonDocument::fromJson(client.readLine()).object();
    };
    auto frame = [&](const QString &path, const QString &reason) {
        QJsonObject o{{"type", "frame"}, {"version", 1}, {"path", path}, {"width", image->width()},
                      {"height", image->height()}, {"name", "Blender: Camera"}, {"reason", reason}};
        sendMsg(o);
        QJsonObject m = readMsg();
        while (!m.isEmpty() && m.value("type").toString() != "ack") m = readMsg(); // skip "canvas" news
        return m;
    };
    auto countLayers = [&](const QString &path) {
        int n = 0;
        KisLayerUtils::recursiveApplyNodes(image->root(), [&](KisNodeSP node) {
            if (KisFileLayer *fl = qobject_cast<KisFileLayer*>(node.data())) {
                if (QFileInfo(fl->path()).absoluteFilePath() == QFileInfo(path).absoluteFilePath()) n++;
            }
        });
        return n;
    };

    sendMsg(QJsonObject{{"type", "hello"}, {"version", 1}, {"app", "selftest"}, {"folder", folder}});
    const QJsonObject canvas = readMsg();
    report(canvas.value("type").toString() == "canvas" && canvas.value("width").toInt() == image->width()
           && canvas.value("height").toInt() == image->height(),
           "blender-canvas-size", QString::fromUtf8(QJsonDocument(canvas).toJson(QJsonDocument::Compact)));

    if (KisView *view = w->activeView()) { // headless: hand the view to the view manager
        view->viewManager()->setCurrentView(view);
        pump(10);
    }
    vm->nodeManager()->slotNonUiActivatedNode(image->root()->firstChild());
    pump(5);
    if (!vm->activeNode() && w->activeView()) {
        // Still none (headless): set the node where Krita reads it.
        w->activeView()->setCurrentNode(image->root()->firstChild());
        w->activeView()->canvasBase()->resourceManager()->setResource(
            KoCanvasResource::CurrentKritaNode, QVariant::fromValue(KisNodeWSP(image->root()->firstChild())));
        pump(5);
    }
    KisNodeSP activeBefore = vm->activeNode();
    report(activeBefore && activeBefore == image->root()->firstChild(), "blender-test-active-set",
           activeBefore ? activeBefore->name() : QStringLiteral("none"));
    const int topBefore = image->root()->childCount();
    QJsonObject ack = frame(png, "send");
    pump(10);
    report(ack.value("result").toString() == "created" && countLayers(png) == 1
           && image->root()->childCount() == topBefore + 1,
           "blender-send-creates-layer", QString::fromUtf8(QJsonDocument(ack).toJson(QJsonDocument::Compact)));
    report(vm->activeNode() == activeBefore, "blender-active-layer-kept",
           vm->activeNode() ? vm->activeNode()->name() : QStringLiteral("none"));
    KisNodeSP added = activeBefore ? activeBefore->nextSibling() : KisNodeSP();
    report(added && qobject_cast<KisFileLayer*>(added.data()) && added->name() == "Blender: Camera",
           "blender-layer-above-active", added ? added->name() : QStringLiteral("nothing above"));

    // The picture arrives in the background: wait until the canvas shows red.
    t.restart();
    QColor c;
    while (t.elapsed() < 5000) {
        pump(2);
        image->projection()->pixel(image->width() / 2, image->height() / 2, &c);
        if (c.red() > 200 && c.green() < 50) break;
    }
    report(c.red() > 200 && c.green() < 50, "blender-picture-shown", c.name());

    ack = frame(png, "send");
    report(ack.value("result").toString() == "updated" && countLayers(png) == 1,
           "blender-send-again-updates", QString::fromUtf8(QJsonDocument(ack).toJson(QJsonDocument::Compact)));

    // Fast path: raw pixels in a shared-memory file, decoded on a worker
    // thread; the file is deleted after reading. Blue this time.
    {
        const QString raw = QDir::home().filePath(QStringLiteral("gigasketch-blender-test-1.rgba"));
        QFile rf(raw);
        rf.open(QIODevice::WriteOnly);
        QByteArray row(image->width() * 4, 0);
        for (int x = 0; x < image->width(); ++x) { row[4 * x + 2] = char(255); row[4 * x + 3] = char(255); }
        for (int y = 0; y < image->height(); ++y) rf.write(row);
        rf.close();
        QElapsedTimer rt; rt.start();
        sendMsg(QJsonObject{{"type", "rendering"}, {"version", 1}});
        pump(5);
        QWidget *bar = bridge->progress()->widget();
        report(bar && bar->isVisible() && bridge->progress()->text().contains("rendering"),
               "blender-progress-rendering", bridge->progress()->text());
        QJsonObject o{{"type", "frame"}, {"version", 1}, {"path", png}, {"width", image->width()},
                      {"height", image->height()}, {"name", "Blender: Camera"}, {"reason", "send"}, {"raw", raw}};
        sendMsg(o);
        QJsonObject rawAck = readMsg();
        while (!rawAck.isEmpty() && rawAck.value("type").toString() != "ack") rawAck = readMsg();
        const qint64 ms = rt.elapsed();
        QColor b;
        added->original()->pixel(image->width() / 2, image->height() / 2, &b);
        report(rawAck.value("result").toString() == "updated" && b.blue() > 200 && b.red() < 50 && !QFile::exists(raw),
               "blender-raw-frame", QStringLiteral("%1 %2 in %3 ms, raw file %4").arg(rawAck.value("result").toString(), b.name())
               .arg(ms).arg(QFile::exists(raw) ? "left" : "deleted"));
        report(bridge->progress()->text().contains("updated"), "blender-progress-updated", bridge->progress()->text());
        // "Nothing changed" never shows a bar.
        QElapsedTimer ft; ft.start();
        while (ft.elapsed() < 2500) pump(1);
        sendMsg(QJsonObject{{"type", "unchanged"}, {"version", 1}});
        pump(5);
        report(!bar || !bar->isVisible(), "blender-progress-hidden", QString());
        // The PNG is the old way: put red back for the checks below.
        sendMsg(QJsonObject{{"type", "frame"}, {"version", 1}, {"path", png}, {"width", image->width()},
                            {"height", image->height()}, {"name", "Blender: Camera"}, {"reason", "send"}});
        QJsonObject back = readMsg();
        while (!back.isEmpty() && back.value("type").toString() != "ack") back = readMsg();
        t.restart();
        while (t.elapsed() < 5000) {
            pump(2);
            added->original()->pixel(image->width() / 2, image->height() / 2, &b);
            if (b.red() > 200) break;
        }
    }

    ack = frame(png2, "refresh");
    report(ack.value("result").toString() == "ignored" && countLayers(png2) == 0,
           "blender-refresh-never-adds", QString::fromUtf8(QJsonDocument(ack).toJson(QJsonDocument::Compact)));

    const int asked = bridge->requestRefresh();
    const QJsonObject refresh = readMsg();
    report(asked == 1 && refresh.value("type").toString() == "refresh" && refresh.value("width").toInt() == image->width(),
           "blender-refresh-request", QString::fromUtf8(QJsonDocument(refresh).toJson(QJsonDocument::Compact)));

    ack = frame(QDir(folder).filePath("missing.png"), "send");
    report(ack.value("result").toString() == "ignored", "blender-missing-file-ignored",
           QString::fromUtf8(QJsonDocument(ack).toJson(QJsonDocument::Compact)));

    // Step 2: the picture is saved inside the .kra. Save, delete the PNG,
    // reopen: the layer still shows red, no "file not found" error, and
    // Convert to Paint Layer (= freeze) keeps the picture.
    KisFileLayer *blenderLayer = qobject_cast<KisFileLayer*>(added.data());
    report(blenderLayer && blenderLayer->gigaIsBlenderLayer(), "blender-layer-flagged", QString());
    const QString kra = QDir::home().filePath(QStringLiteral("blender-test.kra"));
    bool saved = doc->exportDocumentSync(kra, "application/x-krita");
    QFile::remove(png);
    KisDocument *reopened = KisPart::instance()->createDocument();
    const bool opened = saved && reopened->openPath(kra, KisDocument::DontAddToRecent);
    KisFileLayer *loaded = nullptr;
    if (opened) {
        KisLayerUtils::recursiveApplyNodes(reopened->image()->root(), [&](KisNodeSP node) {
            if (KisFileLayer *fl = qobject_cast<KisFileLayer*>(node.data())) loaded = fl;
        });
    }
    QColor saved_c;
    bool noError = false;
    if (loaded) {
        loaded->original()->pixel(image->width() / 2, image->height() / 2, &saved_c);
        noError = true;
        for (const KisBaseNode::Property &p : loaded->sectionModelProperties()) {
            if (p.id == KisLayerPropertiesIcons::layerError.id()) noError = false;
        }
    }
    report(saved && opened && loaded && loaded->gigaIsBlenderLayer(), "blender-kra-reopened",
           QStringLiteral("saved %1, opened %2, layer %3").arg(saved).arg(opened).arg(loaded ? loaded->name() : QStringLiteral("none")));
    report(saved_c.red() > 200 && saved_c.green() < 50, "blender-kra-picture-without-png", saved_c.name());
    report(noError, "blender-kra-no-missing-file-error", QString());
    if (loaded) {
        // Blender sends again (green this time) while that document is shown:
        // the layer picks up the new PNG although it wasn't watching it.
        w->showDocument(reopened);
        if (KisView *view = w->activeView()) { view->viewManager()->setCurrentView(view); }
        pump(10);
        pic.fill(QColor(0, 255, 0));
        pic.save(png);
        ack = frame(png, "send");
        t.restart();
        QColor g;
        while (t.elapsed() < 5000) {
            pump(2);
            loaded->original()->pixel(image->width() / 2, image->height() / 2, &g);
            if (g.green() > 200 && g.red() < 50) break;
        }
        report(ack.value("result").toString() == "updated" && g.green() > 200 && g.red() < 50,
               "blender-reopened-layer-updates", QStringLiteral("%1 %2").arg(ack.value("result").toString(), g.name()));
        pic.fill(QColor(255, 0, 0)); // back to red for the freeze check
        pic.save(png);
        frame(png, "send");
        t.restart();
        while (t.elapsed() < 5000) {
            pump(2);
            loaded->original()->pixel(image->width() / 2, image->height() / 2, &g);
            if (g.red() > 200) break;
        }
        KisNodeSP paint = KisLayerUtils::convertToPaintLayer(reopened->image(), loaded).get();
        reopened->image()->waitForDone();
        QColor pc;
        if (paint && paint->paintDevice()) paint->paintDevice()->pixel(image->width() / 2, image->height() / 2, &pc);
        report(paint && paint->inherits("KisPaintLayer") && pc.red() > 200 && pc.green() < 50,
               "blender-freeze-to-paint-layer", paint ? QStringLiteral("%1 %2").arg(paint->name(), pc.name()) : QStringLiteral("none"));
    }
    // reopened is shown in the window now; the test run ends with the app.

    client.disconnectFromServer();
    t.restart();
    while (bridge->clientCount() > 0 && t.elapsed() < 3000) pump(1);
    report(bridge->clientCount() == 0 && bridge->requestRefresh() == 0, "blender-disconnect", QString());
}

// Giga tool icons: loadIcon() returns our line-art icon when one exists
// (feature/giga-tool-icons).
void testToolIcons(KisMainWindow *w)
{
    Q_UNUSED(w);
    const char *prefix = KisIconUtils::useDarkIcons() ? "dark_" : "light_";
    int same = 0, total = 0;
    for (const char *name : {"krita_tool_freehand", "krita_tool_color_fill", "krita_tool_gradient", "calligraphy"}) {
        const QImage a = KisIconUtils::loadIcon(QLatin1String(name)).pixmap(32, 32).toImage();
        const QImage b = QIcon(QStringLiteral(":/giga-icons/") + QLatin1String(prefix) + QLatin1String(name)
                               + QStringLiteral(".svg")).pixmap(32, 32).toImage();
        total++;
        if (!a.isNull() && a == b) same++;
    }
    report(same == total, "icons-giga-used", QStringLiteral("%1/%2").arg(same).arg(total));
}

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
// Interface motion: reveal() and crossFade() put a cover on top that removes
// itself when the fade ends; nothing when animations are off (feature/ui-motion).
void testMotion(KisMainWindow *w)
{
    auto covers = [](QWidget *on) {
        int n = 0;
        for (QObject *c : on->children()) {
            if (QString::fromLatin1(c->metaObject()->className()) == QLatin1String("QWidget")
                && static_cast<QWidget *>(c)->testAttribute(Qt::WA_TransparentForMouseEvents)
                && static_cast<QWidget *>(c)->geometry() == on->rect()) n++;
        }
        return n;
    };
    auto wait = [](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) pump(1); };
    openTestDocument(w); // a canvas on screen, so the snapshot shows one
    pump(10);
    GigaMotion::setEnabled(true);
    QWidget *target = w->centralWidget();
    if (!target) { report(false, "motion-setup", QStringLiteral("no central widget")); return; }
    const int base = covers(target);
    GigaMotion::reveal(target, 120);
    pump(2);
    report(covers(target) == base + 1, "motion-reveal-cover", QString::number(covers(target) - base));
    wait(400);
    report(covers(target) == base, "motion-reveal-cleans-up", QString::number(covers(target) - base));

    const QPixmap before = GigaMotion::snapshot(w);
    report(!before.isNull(), "motion-snapshot", QStringLiteral("%1x%2").arg(before.width()).arg(before.height()));
    if (!qEnvironmentVariableIsEmpty("GIGA_MOTION_DUMP")) before.save(qEnvironmentVariable("GIGA_MOTION_DUMP"));
    const int wbase = covers(w);
    GigaMotion::crossFade(w, before, 180);
    pump(2);
    report(covers(w) == wbase + 1, "motion-crossfade-cover", QString::number(covers(w) - wbase));
    wait(450);
    report(covers(w) == wbase, "motion-crossfade-cleans-up", QString::number(covers(w) - wbase));

    GigaMotion::setEnabled(false);
    GigaMotion::reveal(target, 120);
    pump(2);
    report(covers(target) == base, "motion-off", QString::number(covers(target) - base));
    GigaMotion::setEnabled(true);
}

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

    // Canvas-style navigation on the Board (bindings from Canvas Input
    // Settings): Shift+Space drag rotates, the reset key turns it back,
    // Ctrl+Space drag zooms toward the press point.
    {
        auto wait = [](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) pump(1); };
        wait(300);
        auto navDrag = [boardView](const QPointF &from, const QPointF &to, Qt::KeyboardModifiers mods) {
            QKeyEvent spaceDown(QEvent::KeyPress, Qt::Key_Space, mods);
            QCoreApplication::sendEvent(boardView, &spaceDown);
            const QPoint a = from.toPoint();
            QMouseEvent press(QEvent::MouseButtonPress, a, boardView->mapToGlobal(a), Qt::LeftButton, Qt::LeftButton, mods);
            QCoreApplication::sendEvent(boardView, &press);
            for (int i = 1; i <= 8; ++i) {
                const QPoint p = (from + (to - from) * i / 8.0).toPoint();
                QMouseEvent move(QEvent::MouseMove, p, boardView->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, mods);
                QCoreApplication::sendEvent(boardView, &move);
            }
            const QPoint b = to.toPoint();
            QMouseEvent release(QEvent::MouseButtonRelease, b, boardView->mapToGlobal(b), Qt::LeftButton, Qt::NoButton, mods);
            QCoreApplication::sendEvent(boardView, &release);
            QKeyEvent spaceUp(QEvent::KeyRelease, Qt::Key_Space, mods);
            QCoreApplication::sendEvent(boardView, &spaceUp);
            pump(3);
        };
        const QPointF mid(boardView->width() / 2.0, boardView->height() / 2.0);
        const qreal r = qMin(mid.x(), mid.y()) * 0.6;

        // A quarter turn: from right of centre to below it = +90°.
        navDrag(mid + QPointF(r, 0), mid + QPointF(0, r), Qt::ShiftModifier);
        const qreal turned = boardView->property("rotation").toReal();
        report(qAbs(turned - 90.0) < 1.0, "board-shift-space-rotate", QStringLiteral("%1°").arg(turned, 0, 'f', 1));

        // Clicking a turned image still hits it.
        QMetaObject::invokeMethod(boardView, "imageWidgetCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, center), Q_ARG(int, 1));
        {
            const QPoint p = center.toPoint();
            QMouseEvent press(QEvent::MouseButtonPress, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(boardView, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(boardView, &release);
            pump(3);
        }
        report(boardView->property("focusedIndex").toInt() == 1, "board-rotated-click-focus",
               QString::number(boardView->property("focusedIndex").toInt()));
        report(qAbs(boardView->property("rotation").toReal() - 90.0) < 1.0, "board-focus-keeps-rotation", QString());
        {
            QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(boardView, &esc);
        }
        wait(400);

        // The canvas "Reset Canvas Rotation" key (F1 here) resets the Board.
        KisAction *reset = w->viewManager()->actionManager()->actionByName(QStringLiteral("reset_canvas_rotation"));
        // (A fresh test config may have none: then the profile's reset key, 5.)
        const QKeySequence resetKey = reset && !reset->shortcut().isEmpty() ? reset->shortcut() : QKeySequence(Qt::Key_5);
        if (!resetKey.isEmpty()) {
            const QKeyCombination combo = resetKey[0];
            QKeyEvent key(QEvent::KeyPress, combo.key(), combo.keyboardModifiers());
            QCoreApplication::sendEvent(boardView, &key);
            QKeyEvent up(QEvent::KeyRelease, combo.key(), combo.keyboardModifiers());
            QCoreApplication::sendEvent(boardView, &up);
            wait(350);
        }
        const qreal afterReset = boardView->property("rotation").toReal();
        report(!resetKey.isEmpty() && qAbs(afterReset) < 0.01, "board-reset-rotation-key",
               QStringLiteral("%1 -> %2°").arg(resetKey.toString()).arg(afterReset, 0, 'f', 2));

        // Ctrl+Space drag up 100 px = twice the zoom, toward the press point.
        wait(100);
        QMetaObject::invokeMethod(boardView, "imageWidgetCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, center), Q_ARG(int, 1));
        const qreal zoomBefore = boardView->property("zoom").toReal();
        navDrag(center, center - QPointF(0, 100), Qt::ControlModifier);
        const qreal ratio = boardView->property("zoom").toReal() / zoomBefore;
        QPointF centerAfter;
        QMetaObject::invokeMethod(boardView, "imageWidgetCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, centerAfter), Q_ARG(int, 1));
        const qreal moved = QLineF(center, centerAfter).length();
        const bool toward = GigaZoomOption::isTowardPointer();
        report(qAbs(ratio - 2.0) < 0.05 && (!toward || moved < 1.5), "board-ctrl-space-zoom",
               QStringLiteral("x%1, press point moved %2 px (toward pointer: %3)").arg(ratio, 0, 'f', 2).arg(moved, 0, 'f', 1).arg(toward));
        {
            QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(boardView, &esc);
        }
        wait(400);

        // Flipping the canvas flips the Board (Look > Flip With Canvas, on by default).
        KisCanvasController *controller = dynamic_cast<KisCanvasController *>(canvas->canvasController());
        if (controller) {
            controller->mirrorCanvas(true);
            pump(5);
            const bool flipped = boardView->property("mirrored").toBool();
            QMetaObject::invokeMethod(boardView, "imageWidgetCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, center), Q_ARG(int, 1));
            {
                const QPoint p = center.toPoint();
                QMouseEvent press(QEvent::MouseButtonPress, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(boardView, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(boardView, &release);
                pump(3);
            }
            report(flipped && boardView->property("focusedIndex").toInt() == 1, "board-flips-with-canvas",
                   QStringLiteral("mirrored %1, clicked image %2").arg(flipped).arg(boardView->property("focusedIndex").toInt()));
            {
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QCoreApplication::sendEvent(boardView, &esc);
            }
            Settings off = Settings::load();
            off.flipWithCanvas = false;
            off.save();
            pump(3);
            const bool stillFlipped = boardView->property("mirrored").toBool();
            off.flipWithCanvas = true;
            off.save();
            controller->mirrorCanvas(false);
            pump(3);
            report(!stillFlipped && !boardView->property("mirrored").toBool(), "board-flip-option-off", QString());
            wait(400);

            // Rotating the canvas turns the Board too (Look > Rotate With Canvas).
            wait(100);
            controller->rotateCanvas(30.0);
            pump(5);
            const qreal followed = boardView->property("rotation").toReal();
            report(qAbs(followed - 30.0) < 0.5, "board-rotates-with-canvas", QStringLiteral("%1°").arg(followed, 0, 'f', 1));
            // Shift+Space on the Board adds its own turn; the reset key over
            // the Board removes only that, back to the canvas angle.
            navDrag(mid + QPointF(r, 0), mid + QPointF(0, r), Qt::ShiftModifier);
            const qreal withOwn = boardView->property("rotation").toReal();
            {
                const QKeyCombination combo = resetKey[0];
                QKeyEvent key(QEvent::KeyPress, combo.key(), combo.keyboardModifiers());
                QCoreApplication::sendEvent(boardView, &key);
                QKeyEvent up(QEvent::KeyRelease, combo.key(), combo.keyboardModifiers());
                QCoreApplication::sendEvent(boardView, &up);
                wait(350);
            }
            const qreal afterOwnReset = boardView->property("rotation").toReal();
            report(qAbs(withOwn - 120.0) < 1.0 && qAbs(afterOwnReset - 30.0) < 0.5, "board-reset-keeps-canvas-angle",
                   QStringLiteral("%1° -> %2°").arg(withOwn, 0, 'f', 1).arg(afterOwnReset, 0, 'f', 1));
            Settings noRotate = Settings::load();
            noRotate.rotateWithCanvas = false;
            noRotate.save();
            pump(3);
            const qreal unfollowed = boardView->property("rotation").toReal();
            noRotate.rotateWithCanvas = true;
            noRotate.save();
            controller->resetCanvasRotation();
            pump(5);
            report(qAbs(unfollowed) < 0.01 && qAbs(boardView->property("rotation").toReal()) < 0.01, "board-rotate-option-off",
                   QStringLiteral("off: %1°").arg(unfollowed, 0, 'f', 1));
        } else {
            report(false, "board-flips-with-canvas", QStringLiteral("no canvas controller"));
        }
    }

    // Delete key over the Board deletes the focused image (also the "clear"
    // action's shortcut, Ctrl+Q for the user); undo brings it back.
    {
        auto wait = [](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) pump(1); };
        auto clickImage = [boardView](int index) {
            QPointF c;
            QMetaObject::invokeMethod(boardView, "imageWidgetCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, c), Q_ARG(int, index));
            const QPoint p = c.toPoint();
            QMouseEvent press(QEvent::MouseButtonPress, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(boardView, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(boardView, &release);
            pump(3);
        };
        auto pressKey = [boardView](const QKeyCombination &combo) {
            QKeyEvent over(QEvent::ShortcutOverride, combo.key(), combo.keyboardModifiers());
            QCoreApplication::sendEvent(boardView, &over);
            const bool claimed = over.isAccepted();
            QKeyEvent key(QEvent::KeyPress, combo.key(), combo.keyboardModifiers());
            QCoreApplication::sendEvent(boardView, &key);
            QKeyEvent up(QEvent::KeyRelease, combo.key(), combo.keyboardModifiers());
            QCoreApplication::sendEvent(boardView, &up);
            pump(5);
            return claimed;
        };
        wait(300);
        const int countBefore = boardView->property("imageCount").toInt();
        clickImage(1);
        const bool claimed = pressKey(QKeyCombination(Qt::NoModifier, Qt::Key_Delete));
        const int countAfter = boardView->property("imageCount").toInt();
        report(claimed && countAfter == countBefore - 1 && boardView->property("focusedIndex").toInt() == -1,
               "board-delete-key", QStringLiteral("%1 -> %2 images, claimed %3").arg(countBefore).arg(countAfter).arg(claimed));
        doc->undoStack()->undo();
        pump(5);
        report(boardView->property("imageCount").toInt() == countBefore && refs[1]->isOnBoard(), "board-delete-undo",
               QString::number(boardView->property("imageCount").toInt()));
        wait(400);

        KisAction *clear = w->viewManager()->actionManager()->actionByName(QStringLiteral("clear"));
        if (clear && !clear->shortcut().isEmpty()) {
            clickImage(1);
            pressKey(clear->shortcut()[0]);
            report(boardView->property("imageCount").toInt() == countBefore - 1, "board-clear-shortcut-deletes",
                   clear->shortcut().toString());
            doc->undoStack()->undo();
            pump(5);
            wait(400);
        } else {
            info(QStringLiteral("board-clear-shortcut-deletes skipped: \"clear\" has no shortcut here"));
        }
    }

    // Multi-select (feature/refboard-multi-select): left-drag on empty space
    // draws a selection rectangle; a selected image drags the whole
    // selection (one undo step); Delete removes them all; Esc lets go.
    {
        auto wait = [](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) pump(1); };
        auto key = [boardView](Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) {
            QKeyEvent over(QEvent::ShortcutOverride, k, m);
            QCoreApplication::sendEvent(boardView, &over);
            const bool claimed = over.isAccepted();
            QKeyEvent press(QEvent::KeyPress, k, m);
            QCoreApplication::sendEvent(boardView, &press);
            QKeyEvent up(QEvent::KeyRelease, k, m);
            QCoreApplication::sendEvent(boardView, &up);
            pump(5);
            return claimed;
        };
        auto centerOf = [boardView](int index) {
            QPointF c;
            QMetaObject::invokeMethod(boardView, "imageWidgetCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, c), Q_ARG(int, index));
            return c;
        };
        QMetaObject::invokeMethod(boardView, "fitAll");
        wait(450);
        const int count = boardView->property("imageCount").toInt();
        // Overview has a 16 px margin: the corners are empty Board.
        const QPoint corner(3, 3);
        const QPoint far(boardView->width() - 3, boardView->height() - 3);
        drag(boardView, corner, boardView->mapToGlobal(far), 8);
        report(count >= 2 && boardView->property("selectedCount").toInt() == count, "board-marquee-selects",
               QStringLiteral("%1 of %2 selected").arg(boardView->property("selectedCount").toInt()).arg(count));
        report(boardView->property("focusedIndex").toInt() == -1 && boardView->property("zoom").toReal() > 0, "board-marquee-no-pan", QString());

        // Drag one selected image: all of them move by the same amount.
        QVector<QPointF> before;
        for (int i = 0; i < count; ++i) before.append(centerOf(i));
        const QPoint start = before[0].toPoint();
        drag(boardView, start, boardView->mapToGlobal(start + QPoint(30, 20)), 6);
        wait(250);
        bool allMoved = true;
        QString moved;
        for (int i = 0; i < count; ++i) {
            const QPointF d = centerOf(i) - before[i];
            moved += QStringLiteral("(%1,%2) ").arg(d.x(), 0, 'f', 1).arg(d.y(), 0, 'f', 1);
            allMoved &= QLineF(d, QPointF(30, 20)).length() < 1.5;
        }
        report(allMoved, "board-move-selection", moved);
        doc->undoStack()->undo(); // one step puts all of them back
        pump(5);
        wait(250);
        bool allBack = true;
        for (int i = 0; i < count; ++i) allBack &= QLineF(centerOf(i), before[i]).length() < 1.5;
        report(allBack, "board-move-selection-undo", QString());

        // Esc clears the selection (and keeps the overview).
        report(key(Qt::Key_Escape) && boardView->property("selectedCount").toInt() == 0, "board-esc-clears-selection",
               QString::number(boardView->property("selectedCount").toInt()));

        // Shift+click adds to the selection, Shift+click again takes it out.
        auto shiftClick = [boardView](const QPointF &c) {
            const QPoint p = c.toPoint();
            QMouseEvent press(QEvent::MouseButtonPress, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
            QCoreApplication::sendEvent(boardView, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, p, boardView->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
            QCoreApplication::sendEvent(boardView, &release);
            pump(3);
        };
        shiftClick(centerOf(0));
        shiftClick(centerOf(1));
        const int afterTwo = boardView->property("selectedCount").toInt();
        shiftClick(centerOf(0));
        bool only1 = false;
        QMetaObject::invokeMethod(boardView, "isImageSelected", Qt::DirectConnection, Q_RETURN_ARG(bool, only1), Q_ARG(int, 1));
        report(afterTwo == 2 && boardView->property("selectedCount").toInt() == 1 && only1 && boardView->property("focusedIndex").toInt() == -1,
               "board-shift-click-toggles", QStringLiteral("%1 then %2").arg(afterTwo).arg(boardView->property("selectedCount").toInt()));

        // Scale handles (feature/refboard-scale): dragging the bottom-right
        // handle of a two-image selection scales both by the same factor,
        // around the top-left corner; one undo step puts them back.
        {
            key(Qt::Key_A, Qt::ControlModifier);
            pump(5);
            QVector<QSizeF> sizes;
            QSizeF size;
            for (int i = 0; i < count; ++i) {
                QMetaObject::invokeMethod(boardView, "imageBoardSize", Qt::DirectConnection, Q_RETURN_ARG(QSizeF, size), Q_ARG(int, i));
                sizes.append(size);
            }
            QPointF tl, br;
            QMetaObject::invokeMethod(boardView, "scaleHandleCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, tl), Q_ARG(int, 0));
            QMetaObject::invokeMethod(boardView, "scaleHandleCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, br), Q_ARG(int, 2));
            // Pull the corner a quarter of the way back toward the opposite one: x0.75.
            const QPoint from = br.toPoint();
            const QPointF target = br + (tl - br) * 0.25;
            drag(boardView, from, boardView->mapToGlobal(target.toPoint()), 6);
            wait(250);
            QPointF tlAfter;
            QMetaObject::invokeMethod(boardView, "scaleHandleCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, tlAfter), Q_ARG(int, 0));
            bool sameFactor = true;
            QString ratios;
            qreal first = -1.0;
            for (int i = 0; i < count; ++i) {
                QMetaObject::invokeMethod(boardView, "imageBoardSize", Qt::DirectConnection, Q_RETURN_ARG(QSizeF, size), Q_ARG(int, i));
                const qreal rw = size.width() / sizes[i].width(), rh = size.height() / sizes[i].height();
                ratios += QStringLiteral("%1/%2 ").arg(rw, 0, 'f', 3).arg(rh, 0, 'f', 3);
                if (first < 0) first = rw;
                sameFactor &= qAbs(rw - first) < 0.01 && qAbs(rh - first) < 0.01;
            }
            report(sameFactor && qAbs(first - 0.75) < 0.03 && QLineF(tl, tlAfter).length() < 1.5, "board-scale-selection",
                   ratios + QStringLiteral("anchor moved %1 px").arg(QLineF(tl, tlAfter).length(), 0, 'f', 1));
            doc->undoStack()->undo();
            pump(5);
            wait(250);
            bool back = true;
            for (int i = 0; i < count; ++i) {
                QMetaObject::invokeMethod(boardView, "imageBoardSize", Qt::DirectConnection, Q_RETURN_ARG(QSizeF, size), Q_ARG(int, i));
                back &= qAbs(size.width() - sizes[i].width()) < 0.01;
            }
            report(back, "board-scale-undo", QString());
            QPointF none;
            key(Qt::Key_Escape);
            QMetaObject::invokeMethod(boardView, "scaleHandleCenter", Qt::DirectConnection, Q_RETURN_ARG(QPointF, none), Q_ARG(int, 0));
            report(none == QPointF(-1, -1), "board-no-handles-without-selection", QString());
        }

        // Ctrl+A selects all; Delete removes all of them in one undo step.
        const bool claimedAll = key(Qt::Key_A, Qt::ControlModifier);
        report(claimedAll && boardView->property("selectedCount").toInt() == count, "board-ctrl-a", QString());
        key(Qt::Key_Delete);
        report(boardView->property("imageCount").toInt() == 0, "board-delete-selection",
               QString::number(boardView->property("imageCount").toInt()));
        doc->undoStack()->undo();
        pump(5);
        wait(100);
        report(boardView->property("imageCount").toInt() == count && refs[1]->isOnBoard(), "board-delete-selection-undo",
               QStringLiteral("%1, layer has %2, on board %3 %4, undo text %5").arg(boardView->property("imageCount").toInt())
                   .arg(layer->shapeCount()).arg(refs[1]->isOnBoard()).arg(refs[2]->isOnBoard()).arg(doc->undoStack()->redoText()));
        key(Qt::Key_Escape);
        wait(400);
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

/// Reference Board image search (feature/refboard-image-search): web image
/// downloads (with fallback addresses, one undo step per batch, drops of web
/// links) and, when built with Qt WebEngine, the search browser: full-size
/// rules, right-click add, Pick mode. Local files only, no internet needed.
void testReferenceBoardSearch(KisMainWindow *w)
{
    KisDocument *doc = openTestDocument(w);
    KisView *view = w->activeView();
    if (!doc || !view || !view->canvasBase()) { report(false, "refsearch-setup", QStringLiteral("no document")); return; }
    QDockWidget *dock = w->dockWidget(QStringLiteral("GigaReferenceBoard"));
    if (!dock) { report(false, "refsearch-setup", QStringLiteral("no Board panel")); return; }
    dock->show();
    dock->raise();
    pump(10);
    QWidget *boardView = dock->findChild<QWidget *>(QStringLiteral("GigaReferenceBoardView"));
    if (!boardView) { report(false, "refsearch-setup", QStringLiteral("no Board view")); return; }
    auto wait = [](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) pump(1); };
    auto count = [boardView]() { return boardView->property("imageCount").toInt(); };
    auto waitForCount = [&](int wanted, int ms) {
        QElapsedTimer t;
        t.start();
        while (count() != wanted && t.elapsed() < ms) pump(1);
        return count() == wanted;
    };
    auto newestWidth = [doc]() {
        KisSharedPtr<KisReferenceImagesLayer> layer = doc->referenceImagesLayer();
        if (!layer || layer->referenceImages().isEmpty()) return 0;
        int best = 0, z = -1;
        for (KisReferenceImage *r : layer->referenceImages()) {
            if (r->zIndex() > z) { z = r->zIndex(); best = r->imagePixelSize().width(); }
        }
        return best;
    };

    // Test pictures on disk.
    const QDir home = QDir::home();
    auto savePng = [&home](const QString &name, int w, int h, const QColor &c) {
        QImage img(w, h, QImage::Format_ARGB32);
        img.fill(c);
        img.save(home.filePath(name), "PNG");
        return QUrl::fromLocalFile(home.filePath(name)).toString();
    };
    const QString small = savePng(QStringLiteral("small.png"), 100, 80, Qt::red);
    const QString big = savePng(QStringLiteral("big.png"), 800, 640, Qt::red);
    const QString other = savePng(QStringLiteral("other.png"), 120, 90, Qt::green);
    const QString big2 = savePng(QStringLiteral("big2.png"), 600, 300, Qt::blue);
    const QString missing = QUrl::fromLocalFile(home.filePath(QStringLiteral("missing.png"))).toString();

    // 1. Download with a fallback: the first address fails, the second works.
    int before = count();
    QList<QStringList> one = {{missing, big}};
    QMetaObject::invokeMethod(boardView, "addWebImages", Qt::DirectConnection, Q_ARG(QList<QStringList>, one), Q_ARG(QUrl, QUrl()));
    bool ok = waitForCount(before + 1, 5000);
    report(ok && newestWidth() == 800, "refsearch-download-fallback",
           QStringLiteral("count %1 -> %2, width %3").arg(before).arg(count()).arg(newestWidth()));

    // 2. Two at once = one undo step.
    before = count();
    QList<QStringList> two = {{small}, {other}};
    QMetaObject::invokeMethod(boardView, "addWebImages", Qt::DirectConnection, Q_ARG(QList<QStringList>, two), Q_ARG(QUrl, QUrl()));
    ok = waitForCount(before + 2, 5000);
    doc->undoStack()->undo();
    pump(5);
    report(ok && count() == before, "refsearch-batch-one-undo", QStringLiteral("after undo %1 (was %2)").arg(count()).arg(before));

    // 3. Nothing works: nothing added, a note says so.
    before = count();
    QList<QStringList> bad = {{missing}};
    QMetaObject::invokeMethod(boardView, "addWebImages", Qt::DirectConnection, Q_ARG(QList<QStringList>, bad), Q_ARG(QUrl, QUrl()));
    wait(500);
    QLabel *status = dock->findChild<QLabel *>(QStringLiteral("GigaReferenceBoardStatus"));
    report(count() == before && status && status->text().contains(QStringLiteral("couldn't")), "refsearch-failed-note",
           status ? status->text() : QStringLiteral("no status label"));

    // 4. A dropped web image (data: address, like Google thumbnails).
    {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        QImage img(64, 32, QImage::Format_ARGB32);
        img.fill(Qt::yellow);
        img.save(&buffer, "PNG");
        QMimeData mime;
        mime.setUrls({QUrl(QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64()))});
        before = count();
        const QPointF at(boardView->width() / 2.0, boardView->height() / 2.0);
        // Qt only delivers a drop to the widget a drag entered first.
        QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(boardView, &enter);
        QDropEvent drop(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(boardView, &drop);
        ok = waitForCount(before + 1, 5000);
        report(ok && newestWidth() == 64, "refsearch-drop-web-image", QStringLiteral("count %1 -> %2, width %3, status %4").arg(before).arg(count()).arg(newestWidth()).arg(status ? status->text() : QString()));
    }

    // 5. The search browser (only when built with Qt WebEngine).
    QToolButton *searchButton = dock->findChild<QToolButton *>(QStringLiteral("GigaImageSearchButton"));
    if (!searchButton) {
        info(QStringLiteral("built without Qt WebEngine: no Search button, browser checks skipped"));
        return;
    }
    QElapsedTimer startup;
    startup.start();
    searchButton->click();
    pump(5);
    QWidget *search = dock->findChild<QWidget *>(QStringLiteral("GigaImageSearchView"));
    report(search && search->isVisible(), "refsearch-opens", QStringLiteral("browser made in %1 ms").arg(startup.elapsed()));
    if (!search) return;

    // A local page: a srcset picture, a plain one, and a Bing-style tile.
    QFile page(home.filePath(QStringLiteral("results.html")));
    page.open(QIODevice::WriteOnly);
    page.write(QStringLiteral(
        "<html><body>"
        "<img id=a src='%1' srcset='%1 100w, %2 800w' width=100>"
        "<img id=b src='%3' width=100>"
        "<a class=iusc m='{\"murl\":\"%4\",\"turl\":\"%3\"}'><img id=c src='%3' width=100></a>"
        "</body></html>").arg(small, big, other, big2).toUtf8());
    page.close();
    QMetaObject::invokeMethod(search, "loadUrl", Qt::DirectConnection, Q_ARG(QUrl, QUrl::fromLocalFile(page.fileName())));
    QElapsedTimer loading;
    loading.start();
    while (!search->property("pageLoaded").toBool() && loading.elapsed() < 15000) pump(1);
    report(search->property("pageLoaded").toBool(), "refsearch-page-loads", QStringLiteral("%1 ms").arg(loading.elapsed()));
    wait(300);

    QString answer;
    QMetaObject::invokeMethod(search, "runScriptForTest", Qt::DirectConnection, Q_RETURN_ARG(QString, answer),
                              Q_ARG(QString, QStringLiteral("window.__giga.candidatesForUrl('https://i.pinimg.com/236x/aa/bb/cc/x.jpg')")));
    report(answer.startsWith(QStringLiteral("https://i.pinimg.com/originals/aa/bb/cc/x.jpg")), "refsearch-pinterest-full-size",
           answer.section(QLatin1Char('\n'), 0, 0));

    // Right-click > Add to Board on the srcset picture: the 800 px one arrives.
    before = count();
    QMetaObject::invokeMethod(search, "addImageByIndex", Qt::DirectConnection, Q_ARG(int, 0));
    ok = waitForCount(before + 1, 5000);
    report(ok && newestWidth() == 800, "refsearch-add-full-size", QStringLiteral("width %1").arg(newestWidth()));

    // Pick mode: mark two (the Bing tile gives its original), add them in one go.
    search->setProperty("pickMode", true);
    QMetaObject::invokeMethod(search, "togglePickByIndex", Qt::DirectConnection, Q_ARG(int, 1));
    QMetaObject::invokeMethod(search, "togglePickByIndex", Qt::DirectConnection, Q_ARG(int, 2));
    QElapsedTimer picking;
    picking.start();
    while (search->property("pickCount").toInt() != 2 && picking.elapsed() < 3000) pump(1);
    report(search->property("pickCount").toInt() == 2, "refsearch-pick-count", QString::number(search->property("pickCount").toInt()));
    before = count();
    QMetaObject::invokeMethod(search, "addSelected", Qt::DirectConnection);
    ok = waitForCount(before + 2, 5000);
    int widest = 0;
    if (KisSharedPtr<KisReferenceImagesLayer> layer = doc->referenceImagesLayer()) {
        for (KisReferenceImage *r : layer->referenceImages()) widest = qMax(widest, r->imagePixelSize().width() == 600 ? 600 : 0);
    }
    report(ok && widest == 600, "refsearch-pick-add", QStringLiteral("count %1 -> %2, Bing original found: %3").arg(before).arg(count()).arg(widest == 600));
    wait(300);
    report(search->property("pickCount").toInt() == 0, "refsearch-pick-cleared", QString::number(search->property("pickCount").toInt()));
    doc->undoStack()->undo();
    pump(5);
    report(count() == before, "refsearch-pick-one-undo", QString::number(count()));

    // Closing the split hides the browser, reopening shows the same one.
    searchButton->click();
    pump(5);
    const bool hidden = !search->isVisible();
    searchButton->click();
    pump(5);
    QWidget *again = dock->findChild<QWidget *>(QStringLiteral("GigaImageSearchView"));
    report(hidden && again == search && search->isVisible(), "refsearch-toggle-keeps-browser", QString());
}

QString s_dumpSuffix;

/// Canvas look: dark surround, and the grid drawn over it only when
/// "Grid over background" is on. Reads the canvas widget's own pixels.
void testCanvasLook(KisMainWindow *w)
{
    KisDocument *doc = openTestDocument(w);
    KisView *view = w->activeView();
    if (!doc || !view || !view->canvasBase()) {
        report(false, "canvaslook-setup", QStringLiteral("no view"));
        return;
    }
    KisConfig cfg(false);
    report(cfg.canvasBorderColor() == QColor(30, 30, 30), "canvaslook-border",
           cfg.canvasBorderColor().name());

    KisGridConfig grid = doc->gridConfig();
    grid.setShowGrid(true);
    grid.setGridType(KisGridConfig::GRID_RECTANGULAR);
    grid.setSpacing(QPoint(64, 64));
    w->viewManager()->gridManager()->setGridConfig(grid);
    info(QStringLiteral("grid main %1 sub %2 spacing %3 x/y active %4/%5")
             .arg(grid.colorMain().name(QColor::HexArgb)).arg(grid.colorSubdivision().name(QColor::HexArgb))
             .arg(grid.spacing().x()).arg(grid.xSpacingActive()).arg(grid.ySpacingActive()));
    view->zoomManager()->slotZoomToFit();
    for (int i = 0; i < 3; i++) view->zoomManager()->slotZoomOut();
    pump(20);

    QWidget *canvas = view->canvasBase()->canvasWidget();
    {
        KisCanvasDecorationSP deco = view->canvasBase()->decoration(QStringLiteral("grid"));
        info(QStringLiteral("grid decoration: %1, visible %2, doc showGrid %3")
                 .arg(deco ? QStringLiteral("yes") : QStringLiteral("no"))
                 .arg(deco && deco->visible()).arg(doc->gridConfig().showGrid()));
    }
    // Paint the grid decoration the way the canvas does (whole widget area,
    // as the OpenGL canvas passes it when the option is on), into an image
    // filled with the surround colour. (Grabbing the OpenGL widget does not
    // include the decorations.)
    KisCanvasDecorationSP gridDeco = view->canvasBase()->decoration(QStringLiteral("grid"));
    const KisCoordinatesConverter *converter = view->canvasBase()->coordinatesConverter();
    QImage lastImage;
    auto bluePixelsOutsideImage = [&]() {
        QImage img(canvas->size(), QImage::Format_ARGB32);
        img.fill(KisConfig(true).canvasBorderColor());
        const QRect imageRect = converter->imageRectInWidgetPixels().toAlignedRect();
        {
            QPainter gc(&img);
            gc.fillRect(imageRect, Qt::white);
            const QRect area = KisConfig(true).gigaGridPaintsBackground() ? img.rect() : imageRect;
            gc.setClipRect(area);
            if (gridDeco) gridDeco->paint(gc, converter->widgetToDocument(QRectF(area)), converter, view->canvasBase());
        }
        if (qEnvironmentVariableIsSet("GIGA_CANVASLOOK_DUMP")) {
            img.save(qEnvironmentVariable("GIGA_CANVASLOOK_DUMP") + "-" + s_dumpSuffix + "-" + QString::number(int(KisConfig(true).gigaGridArea())) + ".png");
        }
        lastImage = img;
        // Background-only clips exactly at the image edge: count "inside" a
        // little within the edge; otherwise a little outside it.
        const bool bgOnly = KisConfig(true).gigaGridArea() == KisConfig::GigaGridBackgroundOnly;
        const QRect inner = bgOnly ? imageRect.adjusted(2, 2, -2, -2) : imageRect.adjusted(-2, -2, 2, 2);
        int blue = 0, dark = 0, inside = 0;
        for (int y = 0; y < img.height(); y++) {
            for (int x = 0; x < img.width(); x++) {
                const QColor c = img.pixelColor(x, y);
                const bool bluish = c.blue() > c.red() + 20;
                if (inner.contains(x, y)) {
                    if (bluish) ++inside;
                } else if (bluish) {
                    ++blue;
                } else if (c.red() < 45 && c.green() < 45 && c.blue() < 45) {
                    ++dark;
                }
            }
        }
        struct Counts { int first; int second; int third; };
        return Counts{blue, dark, inside};
    };
    const QList<QPair<KisGridConfig::GridType, QString>> types = {
        {KisGridConfig::GRID_RECTANGULAR, QStringLiteral("rect")},
        {KisGridConfig::GRID_ISOMETRIC, QStringLiteral("iso")},
        {KisGridConfig::GRID_ISOMETRIC_LEGACY, QStringLiteral("isolegacy")},
    };
    for (const auto &type : types) {
        grid.setGridType(type.first);
        w->viewManager()->gridManager()->setGridConfig(grid);
        s_dumpSuffix = type.second;
        for (bool over : {false, true}) {
            cfg.setGigaGridArea(over ? KisConfig::GigaGridCanvasAndBackground : KisConfig::GigaGridOnCanvas);
            const auto counts = bluePixelsOutsideImage();
            const bool ok = over ? counts.first > 50 : counts.first == 0;
            report(ok && counts.second > 100 && counts.third > 50,
                   qPrintable(QStringLiteral("canvaslook-%1-%2").arg(type.second, over ? "over" : "canvas-only")),
                   QStringLiteral("bluish outside image: %1, inside: %2, dark: %3").arg(counts.first).arg(counts.third).arg(counts.second));
        }
    }

    // Background only: nothing on the image, lines around it, and the same
    // on-screen spacing at two zoom levels.
    grid.setGridType(KisGridConfig::GRID_RECTANGULAR);
    w->viewManager()->gridManager()->setGridConfig(grid);
    cfg.setGigaGridArea(KisConfig::GigaGridBackgroundOnly);
    cfg.setGigaBackgroundGridSize(48);
    s_dumpSuffix = QStringLiteral("bg");
    // Typical gap between bluish pixels along the quietest of the top rows
    // (that row crosses only vertical lines).
    auto onScreenGap = [&]() {
        int bestRow = -1, bestCount = INT_MAX;
        for (int y = 2; y < 40; y++) {
            int n = 0;
            for (int x = 0; x < lastImage.width(); x++) {
                const QColor c = lastImage.pixelColor(x, y);
                if (c.blue() > c.red() + 20) ++n;
            }
            if (n > 0 && n < bestCount) { bestCount = n; bestRow = y; }
        }
        if (bestRow < 0) return -1;
        QList<int> gaps;
        int prev = -1;
        for (int x = 0; x < lastImage.width(); x++) {
            const QColor c = lastImage.pixelColor(x, bestRow);
            if (c.blue() > c.red() + 20) {
                if (prev >= 0 && x - prev > 1) gaps << x - prev;
                prev = x;
            }
        }
        if (gaps.isEmpty()) return -1;
        std::sort(gaps.begin(), gaps.end());
        return gaps[gaps.size() / 2];
    };
    int gaps[2] = {-1, -1};
    for (int i = 0; i < 2; i++) {
        if (i == 1) {
            view->zoomManager()->slotZoomIn();
            pump(20);
        }
        s_dumpSuffix = QStringLiteral("bg-zoom%1").arg(i);
        const auto counts = bluePixelsOutsideImage();
        gaps[i] = onScreenGap();
        report(counts.third == 0 && counts.first > 50,
               qPrintable(QStringLiteral("canvaslook-bgonly-zoom%1").arg(i)),
               QStringLiteral("bluish outside image: %1, inside: %2, gap %3 px").arg(counts.first).arg(counts.third).arg(gaps[i]));
    }
    // 48 px squares with 2 subdivisions: a line every 24 screen px.
    report(gaps[0] >= 23 && gaps[0] <= 25 && gaps[1] >= 23 && gaps[1] <= 25, "canvaslook-bgonly-fixed-size",
           QStringLiteral("gaps %1 / %2 px").arg(gaps[0]).arg(gaps[1]));

    cfg.setGigaGridArea(KisConfig::GigaGridOnCanvas);
}

/// Layers panel bottom row: New, Duplicate, Delete side by side on the left.
// Accent colour: the palette's Highlight follows gigaAccentColor and goes back
// to the theme's colour when it is cleared (feature/accent-color).
void testAccentColor(KisMainWindow *w)
{
    Q_UNUSED(w);
    const QColor themeHighlight = qApp->palette().color(QPalette::Active, QPalette::Highlight);
    const QColor teal(32, 150, 140);
    KisConfig(false).writeEntry<QString>(GigaAccent::configKey(), teal.name());
    KisConfigNotifier::instance()->notifyConfigChanged();
    pump(5);
    const QColor now = qApp->palette().color(QPalette::Active, QPalette::Highlight);
    report(now == teal, "accent-applied", now.name());
    report(qApp->palette().color(QPalette::Active, QPalette::HighlightedText) == QColor(255, 255, 255),
           "accent-text-readable", qApp->palette().color(QPalette::Active, QPalette::HighlightedText).name());
    KisConfig(false).writeEntry<QString>(GigaAccent::configKey(), QString());
    KisConfigNotifier::instance()->notifyConfigChanged();
    pump(5);
    const QColor back = qApp->palette().color(QPalette::Active, QPalette::Highlight);
    report(back == themeHighlight, "accent-theme-default", back.name() + QStringLiteral(" vs ") + themeHighlight.name());
}

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
    const bool adjacent = dup->x() - (add->x() + add->width()) < 12;
    report(visible && order && adjacent, "layerbuttons-order",
           QStringLiteral("x: new %1, duplicate %2, delete %3").arg(add->x()).arg(dup->x()).arg(del->x()));
    // Delete is the rightmost button of the row (user choice 2026-11-17).
    int rightmost = -1;
    for (QToolButton *b : del->parentWidget()->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!b->isHidden() && qAbs(b->y() - del->y()) < 4) rightmost = qMax(rightmost, b->x());
    }
    report(rightmost == del->x(), "layerbuttons-delete-right",
           QStringLiteral("delete x %1, rightmost button x %2").arg(del->x()).arg(rightmost));

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
// Blender layer transform (feature/blender-layer-transform): Ctrl+T on a
// Blender layer puts the transform into a transform mask under it (added on
// first use, reused after), the layer stays active, and a new picture from
// Blender comes out with the same transform.
void testBlenderTransform(KisMainWindow *w)
{
    KisDocument *doc = openTestDocument(w);
    KisView *view = w->activeView();
    KisCanvas2 *canvas = view ? view->canvasBase() : nullptr;
    if (!doc || !canvas) { report(false, "setup", QStringLiteral("no document/canvas")); return; }
    KisImageSP image = doc->image();
    KisViewManager *vm = view->viewManager();
    w->setActiveView(view);
    vm->setCurrentView(view);
    pump(10);

    auto pass = [&](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) { pump(1); QThread::msleep(5); }
        t.restart();
        while (!image->isIdle() && t.elapsed() < 5000) { pump(1); QThread::msleep(5); }
        pump(5);
    };
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    auto picture = [&](const QColor &col) {
        KisPaintDeviceSP dev = new KisPaintDevice(cs);
        dev->fill(QRect(0, 0, image->width(), image->height()), KoColor(col, cs));
        return dev;
    };

    KisFileLayer *layer = new KisFileLayer(image, QString(), QString(), KisFileLayer::ToImageSize,
                                           QStringLiteral("Bicubic"), QStringLiteral("Blender: Camera"), OPACITY_OPAQUE_U8);
    layer->gigaSetBlenderLayer(true);
    KisNodeSP layerNode(layer);
    KisNodeCommandsAdapter(vm).addNode(layerNode, image->root(), image->root()->lastChild());
    layer->gigaSetPicture(picture(Qt::red));
    pass(300);
    vm->nodeManager()->slotNonUiActivatedNode(layerNode);
    pump(5);
    if (vm->activeNode() != layerNode) {
        view->setCurrentNode(layerNode);
        canvas->resourceManager()->setResource(KoCanvasResource::CurrentKritaNode, QVariant::fromValue(KisNodeWSP(layerNode)));
        pump(5);
    }

    const QPoint centre(image->width() / 2, image->height() / 2);
    const QPoint corner(20, 20);
    auto at = [&](const QPoint &p) {
        QColor q;
        image->projection()->pixel(p.x(), p.y(), &q);
        return q;
    };
    auto masks = [&]() { return layerNode->childNodes(QStringList(QStringLiteral("KisTransformMask")), KoProperties()).size(); };
    auto name = [](const QColor &q) { return q.name(QColor::HexArgb); };
    auto isWhite = [](const QColor &q) { return q.red() > 230 && q.green() > 230 && q.blue() > 230; };
    report(at(corner).red() > 200 && at(corner).green() < 50, "blendertf-picture", name(at(corner)));

    // Ctrl+T: scale to 50 % around the center and apply.
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KisToolTransform"));
    pass(500);
    KoToolBase *transform = KoToolManager::instance()->toolById(canvas, QStringLiteral("KisToolTransform"));
    if (!transform) { report(false, "blendertf-tool", QStringLiteral("no transform tool")); return; }
    report(masks() == 1, "blendertf-mask-added", QString::number(masks()));
    report(vm->activeNode() == layerNode, "blendertf-layer-stays-active",
           vm->activeNode() ? vm->activeNode()->name() : QStringLiteral("none"));
    transform->setProperty("scaleX", 50.0);
    transform->setProperty("scaleY", 50.0);
    QMetaObject::invokeMethod(transform, "requestImageRecalculation");
    pass(300);
    static_cast<KisTool *>(transform)->requestStrokeEnd();
    pass(1500);
    report(isWhite(at(corner)) && at(centre).red() > 200, "blendertf-scaled",
           QStringLiteral("corner %1, centre %2").arg(name(at(corner)), name(at(centre))));

    // A new picture from Blender keeps the transform.
    layer->gigaSetPicture(picture(Qt::blue));
    pass(1500);
    report(isWhite(at(corner)) && at(centre).blue() > 200 && at(centre).red() < 50, "blendertf-refresh-keeps-scale",
           QStringLiteral("corner %1, centre %2").arg(name(at(corner)), name(at(centre))));

    // Saved in the .kra: the reopened Blender layer has its mask, still 50 %.
    {
        const QString kra = QDir::home().filePath(QStringLiteral("blendertf-test.kra"));
        const bool saved = doc->exportDocumentSync(kra, "application/x-krita");
        KisDocument *reopened = KisPart::instance()->createDocument();
        const bool opened = saved && reopened->openPath(kra, KisDocument::DontAddToRecent);
        KisFileLayer *loaded = nullptr;
        if (opened) {
            KisLayerUtils::recursiveApplyNodes(reopened->image()->root(), [&](KisNodeSP node) {
                if (KisFileLayer *fl = qobject_cast<KisFileLayer*>(node.data())) loaded = fl;
            });
        }
        KisTransformMask *tm = loaded ? dynamic_cast<KisTransformMask*>(loaded->firstChild().data()) : nullptr;
        const double sc = tm ? tm->transformParams()->finalAffineTransform().m11() : -1.0;
        report(loaded && loaded->gigaIsBlenderLayer() && tm && qAbs(sc - 0.5) < 0.01, "blendertf-kra-roundtrip",
               QStringLiteral("saved %1, opened %2, mask %3, scale %4").arg(saved).arg(opened).arg(tm != nullptr).arg(sc));
        delete reopened;
    }

    // Second Ctrl+T reuses the mask (and picks up its 50 %).
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    pass(300);
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KisToolTransform"));
    pass(500);
    auto maskScale = [&]() {
        KisTransformMask *tm = dynamic_cast<KisTransformMask*>(layerNode->firstChild().data());
        return tm ? tm->transformParams()->finalAffineTransform().m11() : -1.0;
    };
    report(masks() == 1 && qAbs(transform->property("scaleX").toDouble() - 0.5) < 0.01, "blendertf-mask-reused",
           QStringLiteral("%1 mask(s), tool scale %2").arg(masks()).arg(transform->property("scaleX").toDouble()));
    // Esc = Krita's "Reset" (back to before the first transform), saved
    // when the tool is left; Ctrl+Z brings the 50 % back.
    static_cast<KisTool *>(transform)->requestStrokeCancellation();
    pass(500);
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    pass(3000);
    report(at(corner).blue() > 200 && qFuzzyCompare(maskScale(), 1.0), "blendertf-esc-resets",
           QStringLiteral("corner %1, mask scale %2").arg(name(at(corner))).arg(maskScale()));
    doc->undoStack()->undo();
    pass(3000);
    report(isWhite(at(corner)) && qFuzzyCompare(maskScale(), 0.5), "blendertf-undo-reset",
           QStringLiteral("corner %1, mask scale %2").arg(name(at(corner))).arg(maskScale()));
    doc->undoStack()->undo();
    pass(3000);
    report(at(corner).blue() > 200, "blendertf-undo-transform", name(at(corner)));
    doc->undoStack()->undo();
    pass(1000);
    report(masks() == 0 && at(corner).blue() > 200, "blendertf-undo-mask",
           QStringLiteral("%1 mask(s), corner %2").arg(masks()).arg(name(at(corner))));
}

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
        // Held: no repaint (layer without the stroke), the tool draws the
        // last repaint bent to the new place on top.
        bool warping = false;
        QMetaObject::invokeMethod(points, "gigaWarping", Q_RETURN_ARG(bool, warping));
        const QColor layerNew = pixel((at + QPointF(0, 40)).toPoint());
        const QColor layerOld = pixel(at.toPoint());
        QImage overlay(cw->size(), QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        {
            QPainter p(&overlay);
            p.setTransform(conv->flakeToWidgetTransform());
            points->paint(p, *canvas->viewConverter());
        }
        // A little beside the held dot (the dot itself is drawn on top there).
        const QPoint shownAt = toWidget(at + QPointF(0, 40)) + QPoint(10, 0);
        const QPoint oldAt = toWidget(at) + QPoint(10, 0);
        const QColor drawnNew = overlay.pixelColor(shownAt);
        const QColor drawnOld = overlay.pixelColor(oldAt);
        report(warping && isEmpty(layerNew) && isEmpty(layerOld) && isRed(drawnNew) && drawnOld.alpha() < 30,
               "strokepoints-bent-preview-while-dragging",
               QStringLiteral("warping %1; layer new %2 old %3; preview new %4 old %5")
                   .arg(warping).arg(name(layerNew), name(layerOld), name(drawnNew), name(drawnOld)));
        send(2, b);
        wait(800);
        QMetaObject::invokeMethod(points, "gigaWarping", Q_RETURN_ARG(bool, warping));
        report(!warping && isRed(pixel((at + QPointF(0, 40)).toPoint())) && isEmpty(pixel(at.toPoint())),
               "strokepoints-repaint-on-release",
               QStringLiteral("warping %1, layer new %2, old %3").arg(warping)
                   .arg(name(pixel((at + QPointF(0, 40)).toPoint())), name(pixel(at.toPoint()))));
    }
    // Small radius, long drag: the bent preview must stay near the line (it
    // used to blow up into a huge fan: a perspective map of an over-stretched
    // piece). Count preview pixels far away from line, dot and drag.
    {
        QMetaObject::invokeMethod(points, "gigaSetProportional", Q_ARG(bool, true), Q_ARG(int, 15), Q_ARG(int, 0));
        const QPointF at = vertexPos(pmid);
        const QPointF target = at + QPointF(-120, -260);
        const QPoint a = toWidget(at);
        const QPoint b = toWidget(target);
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
        pass(400, false);
        QImage overlay(cw->size(), QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        {
            QPainter p(&overlay);
            p.setTransform(conv->flakeToWidgetTransform());
            points->paint(p, *canvas->viewConverter());
        }
        // Allowed area: the line, the drag and the dot, plus a generous margin.
        const QRectF lineArea = QRectF(toWidget(at + QPointF(-r * 2.0, 0)), toWidget(at + QPointF(r * 2.0, 0))).normalized()
                | QRectF(QPointF(b), QSizeF(1, 1)) | QRectF(QPointF(a), QSizeF(1, 1));
        const QRect allowed = lineArea.toAlignedRect().adjusted(-80, -80, 80, 80);
        int stray = 0;
        for (int y = 0; y < overlay.height(); y += 2) {
            for (int x = 0; x < overlay.width(); x += 2) {
                if (!allowed.contains(x, y) && qAlpha(overlay.pixel(x, y)) > 60) ++stray;
            }
        }
        send(2, b);
        wait(800);
        report(stray == 0, "strokepoints-no-fan-on-small-radius",
               QStringLiteral("%1 preview pixels far from the line (sampled every 2 px)").arg(stray));
        stack->undo();
        wait(600);
    }
    static_cast<KisTool *>(points)->requestStrokeCancellation();
    wait(600);
    KisConfig(false).writeEntry<int>("gigaLastStrokeEditMode", 0);
}

/// A shortcut the user gave to Alt+<letter> wins over a menu's Alt+letter
/// (e.g. Feather Selection on Alt+A vs "&Arrange & Documents").
void testMenuLetter(KisMainWindow *w)
{
    openTestDocument(w);
    KisView *view = w->activeView();
    KisAction *feather = view ? view->viewManager()->actionManager()->actionByName(QStringLiteral("featherselection")) : nullptr;
    // The top-level menu is "&Arrange && Documents" or "&Window", depending
    // on the layout earlier tests left behind. Use whichever is there.
    QAction *menu = nullptr;
    for (QAction *m : w->menuBar()->actions()) {
        if (m->text() == QStringLiteral("&Arrange && Documents") || m->text() == QStringLiteral("&Window")) menu = m;
    }
    if (!feather || !menu) {
        QStringList titles;
        for (QAction *m : w->menuBar()->actions()) titles << m->text();
        report(false, "menuletter-setup", QStringLiteral("feather %1, menu %2; menus: %3")
                                              .arg(!!feather).arg(!!menu).arg(titles.join(QStringLiteral(" | "))));
        return;
    }
    const QString before = menu->text();
    const QString letter = before.mid(1, 1); // the character after the leading '&'
    const QString altKey = QStringLiteral("Alt+") + letter;
    // Same path as Settings > Keyboard Shortcuts > OK: write the custom
    // shortcut file, then let the registry reload it and update every action.
    // (Setting the action directly would be undone by KisPart::updateShortcuts.)
    // selftest.sh runs in a throw-away config, so this never touches yours.
    const QList<QKeySequence> oldKeys = feather->shortcuts();
    KConfigGroup custom(KSharedConfig::openConfig(GigaBranding::shortcutsConfigFile()), QStringLiteral("Shortcuts"));
    auto shortcutsChanged = [&]() {
        custom.sync();
        KisActionRegistry::instance()->settingsPageSaved();
        pump(5);
    };
    custom.writeEntry("featherselection", altKey);
    shortcutsChanged();
    const QString during = menu->text();
    report(!during.contains(QStringLiteral("&") + letter) && during == QString(before).remove(0, 1)
               && feather->shortcut() == QKeySequence(altKey),
           "menuletter-user-shortcut-wins", QStringLiteral("%1: menu \"%2\" -> \"%3\"").arg(altKey, before, during));
    // Write the old key back (deleting the only entry would leave an empty
    // group, which the registry skips reloading).
    custom.writeEntry("featherselection", oldKeys.isEmpty() ? QStringLiteral("none")
                                                            : QKeySequence::listToString(oldKeys));
    shortcutsChanged();
    report(menu->text() == before, "menuletter-restored", QStringLiteral("menu \"%1\"").arg(menu->text()));
}

// Brush outline fade: hidden-while-painting outline fades to 0 during a stroke
// and back to 1 when hovering again (feature/brush-cursor-look).
void testCursorLook(KisMainWindow *w)
{
    {
        KisConfig cfg(false);
        cfg.setShowOutlineWhilePainting(false);   // the fade only runs when painting hides the outline
        cfg.setGigaTwoToneCursor(true);
        cfg.setGigaFadeOutlineWhilePainting(true);
    }
    KisDocument *doc = openTestDocument(w);
    KisView *view = w->activeView();
    KisCanvas2 *canvas = view ? view->canvasBase() : nullptr;
    if (!doc || !canvas) { report(false, "setup", QStringLiteral("no document/canvas")); return; }
    w->setActiveView(view);
    view->viewManager()->setCurrentView(view);
    pump(10);
    KisNodeSP layer = doc->image()->root()->lastChild();
    if (layer) {
        view->viewManager()->nodeManager()->slotNonUiActivatedNode(layer);
        if (!view->currentNode()) {
            view->setCurrentNode(layer);
            canvas->resourceManager()->setResource(KoCanvasResource::CurrentKritaNode, QVariant::fromValue(KisNodeWSP(layer)));
        }
    }
    const QString brushId = QStringLiteral("KritaShape/KisToolBrush");
    KoToolManager::instance()->switchToolRequested(brushId);
    pump(20);
    KisToolPaint *tool = dynamic_cast<KisToolPaint *>(KoToolManager::instance()->toolById(canvas, brushId));
    if (!tool) { report(false, "cursorlook-tool", QStringLiteral("no brush tool")); return; }
    tool->optionWidgets();
    pump(5);

    QWidget *cw = canvas->canvasWidget();
    const QPoint c = cw->rect().center();
    auto event = [&](QEvent::Type type, const QPoint &p, Qt::MouseButtons buttons) {
        QMouseEvent e(type, p, cw->mapToGlobal(p), Qt::LeftButton, buttons, Qt::NoModifier);
        return KoPointerEvent(&e, canvas->coordinatesConverter()->widgetToDocument(QPointF(p)));
    };
    auto wait = [&](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) pump(1);
    };
    {
        KoPointerEvent hover = event(QEvent::MouseMove, c, Qt::NoButton);
        tool->mouseMoveEvent(&hover);
    }
    wait(250);
    const qreal before = tool->gigaOutlineOpacity();
    {
        KoPointerEvent press = event(QEvent::MouseButtonPress, c, Qt::LeftButton);
        tool->beginPrimaryAction(&press);
    }
    for (int i = 1; i <= 10; i++) {
        KoPointerEvent move = event(QEvent::MouseMove, c + QPoint(i * 4, i), Qt::LeftButton);
        tool->continuePrimaryAction(&move);
        wait(30);
    }
    const qreal during = tool->gigaOutlineOpacity();
    {
        KoPointerEvent release = event(QEvent::MouseButtonRelease, c + QPoint(40, 10), Qt::NoButton);
        tool->endPrimaryAction(&release);
    }
    doc->image()->waitForDone();
    {
        KoPointerEvent hover = event(QEvent::MouseMove, c + QPoint(42, 10), Qt::NoButton);
        tool->mouseMoveEvent(&hover);
    }
    wait(300);
    const qreal after = tool->gigaOutlineOpacity();
    report(before > 0.99, "cursorlook-shown-on-hover", QString::number(before));
    report(during < 0.01, "cursorlook-faded-while-painting", QString::number(during));
    report(after > 0.99, "cursorlook-back-after-stroke", QString::number(after));
}

/// Sticky notes (feature/annotations): Edit > Add Note, look, colour/size
/// undo, width resize re-wraps, paste text, copy, duplicate, right-click menu.
void testNotes(KisMainWindow *w)
{
    KisDocument *doc = openTestDocument(w);
    KisView *view = w->activeView();
    KisCanvas2 *canvas = view ? view->canvasBase() : nullptr;
    if (!doc || !canvas) { report(false, "setup", QStringLiteral("no document/canvas")); return; }

    // Edit > Add Note from the brush tool.
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    pump(10);
    KisAction *addAction = view->viewManager()->actionManager()->actionByName(QStringLiteral("giga_add_note"));
    report(addAction != nullptr, "notes-action", addAction ? addAction->text() : QStringLiteral("missing"));
    if (!addAction) return;
    QMenu *editMenu = nullptr;
    Q_FOREACH (QAction *top, w->menuBar()->actions()) {
        if (top->menu() && top->menu()->actions().contains(addAction)) editMenu = top->menu();
    }
    report(editMenu != nullptr, "notes-in-menu", editMenu ? editMenu->title() : QStringLiteral("not in any menu"));
    addAction->trigger();
    pump(10);
    KisSharedPtr<KisReferenceImagesLayer> layer = doc->referenceImagesLayer();
    QVector<GigaNote *> notes = GigaNote::notesOf(layer.data());
    report(notes.size() == 1, "notes-added", QString::number(notes.size()));
    report(KoToolManager::instance()->activeToolId() == QLatin1String("ToolReferenceImages"), "notes-tool-switch",
           KoToolManager::instance()->activeToolId());
    if (notes.size() != 1) return;
    GigaNote *note = notes.first();
    KoSelection *selection = layer->shapeManager()->selection();
    report(selection->isSelected(note), "notes-selected", QString());

    // Typing: the editor opened on the new note. B must type, not switch tools.
    {
        QPointer<GigaNoteEditor> editor = GigaNoteEditor::current();
        report(editor && editor->note() == note, "notes-editor-open", editor ? QStringLiteral("open") : QStringLiteral("none"));
        if (!editor) return;
        QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        QCoreApplication::sendEvent(editor, &override);
        report(override.isAccepted(), "notes-editor-takes-keys", QString());
        QKeyEvent saveOverride(QEvent::ShortcutOverride, Qt::Key_S, Qt::ControlModifier);
        QCoreApplication::sendEvent(editor, &saveOverride);
        report(!saveOverride.isAccepted(), "notes-editor-lets-save-through", QString());
        {
            // The un-taken Ctrl+S went on to the canvas, which now thinks S is held: let it go.
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_S, Qt::ControlModifier);
            QCoreApplication::sendEvent(canvas->canvasWidget(), &release);
        }
        for (const QChar c : QStringLiteral("Hello b")) {
            const int key = c.isSpace() ? int(Qt::Key_Space) : int(Qt::Key_A) + (c.toLower().unicode() - 'a');
            QKeyEvent press(QEvent::KeyPress, key, c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier, QString(c));
            QCoreApplication::sendEvent(editor, &press);
        }
        pump(3);
        report(note->plainText() == QLatin1String("Hello b"), "notes-typing-live", note->plainText());
        if (!editor) { report(false, "notes-editor-stays-open", QStringLiteral("closed while typing")); return; }
        // Ctrl+B makes the next text bold.
        QKeyEvent bold(QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier);
        QCoreApplication::sendEvent(editor, &bold);
        QKeyEvent x(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
        QCoreApplication::sendEvent(editor, &x);
        pump(3);
        report(note->html().contains(QLatin1String("font-weight:700")) || note->html().contains(QLatin1String("font-weight:600")),
               "notes-bold", note->plainText());
        const int undoBefore = doc->undoStack()->index();
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(editor, &esc);
        pump(5);
        report(!GigaNoteEditor::current() && !note->isEditing(), "notes-editor-esc", QString());
        report(doc->undoStack()->index() == undoBefore + 1 && note->plainText() == QLatin1String("Hello bx"),
               "notes-edit-one-undo-step", doc->undoStack()->text(doc->undoStack()->index() - 1));
        report(KoToolManager::instance()->activeToolId() == QLatin1String("ToolReferenceImages"), "notes-b-no-tool-switch",
               KoToolManager::instance()->activeToolId());
        doc->undoStack()->undo();
        pump(3);
        const bool undone = note->isEmpty();
        doc->undoStack()->redo();
        pump(3);
        report(undone && note->plainText() == QLatin1String("Hello bx"), "notes-edit-undo-redo", note->plainText());

        // A new note left empty disappears.
        GigaNoteActions::addNote(canvas, QPointF(10, 10), QString(), true);
        pump(3);
        const int withEmpty = GigaNote::notesOf(layer.data()).size();
        GigaNoteEditor::finishCurrent();
        pump(5);
        report(withEmpty == 2 && GigaNote::notesOf(layer.data()).size() == 1, "notes-empty-new-removed",
               QStringLiteral("%1 -> %2").arg(withEmpty).arg(GigaNote::notesOf(layer.data()).size()));

        // Edit > Add Note, then a click on the new note's strip (to move it): the
        // editor closes but the empty note stays, selected.
        {
            addAction->trigger();
            pump(5);
            GigaNote *fresh = GigaNoteEditor::current() ? GigaNoteEditor::current()->note() : nullptr;
            { QElapsedTimer gt; gt.start(); while (gt.elapsed() < 300) pump(1); }
            QWidget *cw = canvas->canvasWidget();
            if (fresh) {
                const QPointF onStrip = fresh->absoluteTransformation().map(fresh->stripRect().center());
                const QPointF p = canvas->coordinatesConverter()->documentToWidget(onStrip);
                QEnterEvent enter(p, p, cw->mapToGlobal(p));
                QCoreApplication::sendEvent(cw, &enter);
                QMouseEvent press(QEvent::MouseButtonPress, p, cw->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(cw, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, p, cw->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(cw, &release);
                pump(5);
            }
            const bool kept = fresh && layer->shapes().contains(fresh) && !GigaNoteEditor::current();
            report(kept, "notes-click-on-new-note-keeps-it", fresh ? QStringLiteral("in layer %1, editor %2")
                       .arg(layer->shapes().contains(fresh)).arg(GigaNoteEditor::current() != nullptr) : QStringLiteral("no editor"));
            if (kept) {
                // Tidy up so the counts below stay as before.
                canvas->addCommand(layer->removeReferenceImages(doc, {fresh}));
                pump(3);
            }
        }

        // Double-click opens the editor again.
        KoToolBase *refTool = KoToolManager::instance()->toolById(canvas, QStringLiteral("ToolReferenceImages"));
        const QPointF inside = note->absoluteTransformation().map(note->textRect().center());
        const QPointF widgetPoint = canvas->coordinatesConverter()->documentToWidget(inside);
        QMouseEvent dbl(QEvent::MouseButtonDblClick, widgetPoint, canvas->canvasWidget()->mapToGlobal(widgetPoint),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        KoPointerEvent pe(&dbl, inside);
        if (refTool) refTool->mouseDoubleClickEvent(&pe);
        pump(3);
        report(GigaNoteEditor::current() && GigaNoteEditor::current()->note() == note, "notes-double-click-edits", QString());
        // Middle-button (pan) keeps it open; a left click on the canvas closes it.
        { QElapsedTimer gt; gt.start(); while (gt.elapsed() < 300) pump(1); } // clicks right after opening are ignored
        QWidget *cw = canvas->canvasWidget();
        const QPointF far(5, 5);
        QMouseEvent middle(QEvent::MouseButtonPress, far, cw->mapToGlobal(far), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
        QCoreApplication::sendEvent(cw, &middle);
        QMouseEvent middleUp(QEvent::MouseButtonRelease, far, cw->mapToGlobal(far), Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(cw, &middleUp);
        pump(5);
        const bool stillOpen = GigaNoteEditor::current() != nullptr;
        QMouseEvent left(QEvent::MouseButtonPress, far, cw->mapToGlobal(far), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(cw, &left);
        QMouseEvent leftUp(QEvent::MouseButtonRelease, far, cw->mapToGlobal(far), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(cw, &leftUp);
        pump(5);
        report(stillOpen && !GigaNoteEditor::current() && !note->isEditing(), "notes-click-outside-finishes",
               QStringLiteral("after middle: %1").arg(stillOpen));
        GigaNoteEditor::finishCurrent();
        pump(5);
    }

    // Notes are drawn by their own layer above the grid, guides and assistants.
    {
        KisCanvasDecorationSP top = canvas->decoration(QStringLiteral("gigaNotesDecoration"));
        KisCanvasDecorationSP grid = canvas->decoration(QStringLiteral("grid"));
        report(top && top->visible() && (!grid || top->priority() > grid->priority()) && top->priority() > 95,
               "notes-above-grid", top ? QString::number(top->priority()) : QStringLiteral("missing"));
    }

    // Same size on screen as interface text: ~220 screen px wide.
    const qreal screenWidth = canvas->coordinatesConverter()->documentToWidget(note->boundingRect()).width();
    report(qAbs(screenWidth - 220.0) < 3.0, "notes-screen-size", QString::number(screenWidth));

    // It draws its colour.
    {
        QImage img(note->size().toSize() + QSize(2, 2), QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter gc(&img);
        note->paintOnTop(gc);
        gc.end();
        const QColor mid = img.pixelColor(img.width() / 2, img.height() - qMax(3, img.height() / 6));
        report(qAbs(mid.red() - note->color().red()) < 8 && mid.alpha() > 250, "notes-paint",
               mid.name(QColor::HexArgb) + QStringLiteral(" vs ") + note->color().name());
    }

    // Colour change is one undo step.
    const QColor before = note->color();
    const QColor blue = GigaNote::presetColors().at(3);
    GigaNoteActions::changeNotes(canvas, {note}, kundo2_noi18n("colour"), [blue](GigaNote::State &st) { st.color = blue; });
    pump(3);
    const bool changed = note->color() == blue;
    doc->undoStack()->undo();
    pump(3);
    const bool undone = note->color() == before;
    doc->undoStack()->redo();
    pump(3);
    report(changed && undone && note->color() == blue, "notes-colour-undo", note->color().name());

    // Long text in a narrower note wraps onto more lines: taller.
    note->setHtml(QStringLiteral("<p>one two three four five six seven eight nine ten eleven twelve</p>"));
    const qreal wideHeight = note->size().height();
    note->setSize(QSizeF(note->size().width() * 0.5, 1.0));
    const qreal narrowHeight = note->size().height();
    report(narrowHeight > wideHeight * 1.3, "notes-width-rewraps",
           QStringLiteral("%1 -> %2").arg(wideHeight).arg(narrowHeight));

    // Text size: Large is taller.
    GigaNoteActions::changeNotes(canvas, {note}, kundo2_noi18n("size"), [](GigaNote::State &st) { st.textSize = GigaNote::Large; });
    report(note->size().height() > narrowHeight * 1.2, "notes-text-size",
           QStringLiteral("%1 -> %2").arg(narrowHeight).arg(note->size().height()));

    // Styles: one undo step each, remembered for new notes, each draws its look.
    {
        GigaNoteActions::applyStyle(canvas, {note}, GigaNote::Card);
        pump(3);
        const bool card = note->style() == GigaNote::Card && GigaNote::defaultStyle() == GigaNote::Card;
        doc->undoStack()->undo();
        pump(3);
        const bool undone = note->style() == GigaNote::Sticky;
        doc->undoStack()->redo();
        pump(3);
        report(card && undone && note->style() == GigaNote::Card, "notes-style-undo", QString::number(note->style()));
        GigaNote *fresh = GigaNote::create(QPointF(), 1.0, QStringLiteral("x"));
        report(fresh->style() == GigaNote::Card, "notes-style-default-remembered", QString::number(fresh->style()));
        delete fresh;

        // Render all four on a mid-grey background.
        const QStringList names = GigaNote::styleNames();
        QImage sheet(4 * 240 + 20, 200, QImage::Format_ARGB32);
        sheet.fill(QColor(110, 110, 110));
        QPainter gc(&sheet);
        bool looks = true;
        QString detail;
        for (int st = GigaNote::Sticky; st <= GigaNote::Outline; ++st) {
            GigaNote *n = GigaNote::create(QPointF(), 1.0, QStringLiteral("%1 note: a few words that wrap onto two lines").arg(names[st]));
            n->setStyle(st);
            n->setColor(GigaNote::presetColors().at(st));
            gc.save();
            gc.translate(10 + st * 240, 10);
            n->paintOnTop(gc);
            gc.restore();
            // Pixel just inside the left edge, halfway down the text area.
            const QPointF probe(10 + st * 240 + 2.5, 10 + n->textRect().center().y());
            const QColor got = sheet.pixelColor(probe.toPoint());
            bool ok = true;
            if (st == GigaNote::Card) ok = qAbs(got.red() - n->color().red()) < 12; // the bar
            else if (st == GigaNote::Outline) ok = qAbs(got.red() - 110) < 20 || got != n->color(); // see-through
            else ok = qAbs(got.red() - n->color().red()) < 12;
            if (st == GigaNote::Outline) {
                const QColor inside = sheet.pixelColor(QPoint(10 + st * 240 + 120, 10 + int(n->size().height()) - 4));
                ok = inside.red() < 100 && inside.red() > 40; // darkened, but the background shows through
            }
            looks &= ok;
            detail += QStringLiteral("%1:%2 ").arg(st).arg(got.name());
            delete n;
        }
        gc.end();
        const QString out = qEnvironmentVariable("GIGA_NOTES_RENDER");
        if (!out.isEmpty()) sheet.save(out);
        report(looks, "notes-styles-draw", detail);
    }

    // Collapse shows one line.
    GigaNoteActions::changeNotes(canvas, {note}, kundo2_noi18n("collapse"), [](GigaNote::State &st) { st.collapsed = true; });
    const qreal collapsed = note->size().height();
    GigaNoteActions::changeNotes(canvas, {note}, kundo2_noi18n("expand"), [](GigaNote::State &st) { st.collapsed = false; });
    report(collapsed < note->size().height() * 0.6, "notes-collapse",
           QStringLiteral("%1 vs %2").arg(collapsed).arg(note->size().height()));

    KoToolBase *tool = KoToolManager::instance()->toolById(canvas, QStringLiteral("ToolReferenceImages"));
    if (!tool) { report(false, "notes-tool", QStringLiteral("missing")); return; }

    // Copy puts the note's text on the clipboard; paste of text makes a note.
    selection->deselectAll();
    selection->select(note);
    tool->copy();
    report(QApplication::clipboard()->text().contains(QLatin1String("eleven twelve")), "notes-copy",
           QApplication::clipboard()->text().left(40));
    QApplication::clipboard()->setText(QStringLiteral("Pasted idea"));
    pump(3);
    tool->paste();
    pump(5);
    notes = GigaNote::notesOf(layer.data());
    report(notes.size() == 2 && notes.last()->plainText() == QLatin1String("Pasted idea"), "notes-paste-text",
           QString::number(notes.size()));

    // Right-click menu: Add Note always, note items with a note selected.
    selection->deselectAll();
    selection->select(note);
    QMenu *menu = tool->popupActionsMenu();
    QStringList texts;
    if (menu) {
        Q_FOREACH (QAction *a, menu->actions()) texts << a->text().remove(QLatin1Char('&'));
    }
    report(texts.contains(QStringLiteral("Add Note")) && texts.contains(QStringLiteral("Note Colour"))
               && texts.contains(QStringLiteral("Duplicate Note")),
           "notes-context-menu", texts.join(QStringLiteral(", ")).right(160));

    // Duplicate: one more note, offset, selected.
    GigaNoteActions::duplicateNotes(canvas, {note});
    pump(5);
    notes = GigaNote::notesOf(layer.data());
    const bool dup = notes.size() == 3 && notes.last()->html() == note->html()
        && notes.last()->position().x() > note->position().x() && selection->isSelected(notes.last());
    report(dup, "notes-duplicate", QString::number(notes.size()));

    // Delete via the tool.
    tool->deleteSelection();
    pump(5);
    report(GigaNote::notesOf(layer.data()).size() == 2, "notes-delete",
           QString::number(GigaNote::notesOf(layer.data()).size()));

    // Normalize leaves notes alone.
    const QTransform noteTransform = note->transformation();
    QImage img(400, 300, QImage::Format_ARGB32);
    img.fill(Qt::red);
    KisReferenceImage *picture = KisReferenceImage::fromQImage(*canvas->coordinatesConverter(), img);
    picture->setZIndex(layer->shapes().size());
    canvas->addCommand(KisReferenceImagesLayer::addReferenceImages(doc, {picture}));
    pump(5);
    QVector<KisReferenceImage *> all = layer->referenceImages();
    if (GigaReferenceBoard::GeometryCommand *cmd = GigaReferenceBoard::normalizeOnCanvas(all, GigaReferenceLayout::NormalizeMode::SameArea, nullptr)) {
        canvas->addCommand(cmd);
    }
    pump(5);
    report(note->transformation() == noteTransform, "notes-normalize-skips", QString());

    // View > Show Annotations hides notes (not pictures) and is remembered.
    KisAction *showAction = view->viewManager()->actionManager()->actionByName(QStringLiteral("giga_show_annotations"));
    report(showAction && showAction->isChecked(), "notes-show-action", showAction ? QStringLiteral("checked") : QStringLiteral("missing"));
    if (showAction) {
        showAction->setChecked(false);
        pump(3);
        bool hidden = !GigaNote::notesShown() && picture->isVisible();
        for (GigaNote *n : GigaNote::notesOf(layer.data())) hidden &= !n->isVisible();
        report(hidden, "notes-hidden", QString());
        // Adding a note while hidden shows them again.
        GigaNoteActions::addNote(canvas, QPointF(5, 5), QStringLiteral("again"));
        pump(3);
        bool shown = GigaNote::notesShown() && showAction->isChecked();
        for (GigaNote *n : GigaNote::notesOf(layer.data())) shown &= n->isVisible();
        report(shown, "notes-add-shows-again", QString());
    }

    // Keep readable: same width on screen after zooming in; a normal note grows.
    GigaNote *other = GigaNote::notesOf(layer.data()).last();
    GigaNoteActions::changeNotes(canvas, {note}, kundo2_noi18n("readable"), [](GigaNote::State &st) { st.keepReadable = true; });
    GigaNoteActions::applyKeepReadable(canvas);
    pump(3);
    auto screenW = [canvas](GigaNote *n) { return canvas->coordinatesConverter()->documentToWidget(n->boundingRect()).width(); };
    const qreal readableBefore = screenW(note);
    const qreal otherBefore = screenW(other);
    view->zoomManager()->slotZoomIn();
    view->zoomManager()->slotZoomIn();
    pump(10);
    report(qAbs(screenW(note) - readableBefore) < 1.0 && screenW(other) > otherBefore * 1.3, "notes-keep-readable",
           QStringLiteral("readable %1 -> %2, other %3 -> %4").arg(readableBefore).arg(screenW(note)).arg(otherBefore).arg(screenW(other)));

    // Arrange with "Keep notes in place" (default) moves only pictures.
    {
        GigaReferenceBoard::Settings settings = GigaReferenceBoard::Settings::load();
        report(settings.keepNotesInPlace, "notes-keep-in-place-default", QString());
        const QTransform before = other->transformation();
        if (GigaReferenceBoard::GeometryCommand *cmd = GigaReferenceBoard::arrangeOnCanvas(
                layer->referenceImages(), settings, canvas, layer.data())) {
            canvas->addCommand(cmd);
        }
        pump(5);
        report(other->transformation() == before, "notes-arrange-keeps-notes", QString());
    }

    // Send a note to the Board and back.
    QDockWidget *dock = w->dockWidget(QStringLiteral("GigaReferenceBoard"));
    QWidget *boardView = dock ? dock->findChild<QWidget *>(QStringLiteral("GigaReferenceBoardView")) : nullptr;
    if (dock) { dock->show(); dock->raise(); }
    pump(10);
    canvas->addCommand(GigaReferenceBoard::sendToBoard({other}, layer.data(), GigaReferenceBoard::Settings::load()));
    pump(10);
    report(other->isOnBoard() && !other->isVisible() && (!boardView || boardView->property("imageCount").toInt() == 1),
           "notes-to-board", boardView ? QString::number(boardView->property("imageCount").toInt()) : QStringLiteral("no view"));
    if (boardView) {
        // The Board draws it (no crash, something painted).
        QImage shot(boardView->size(), QImage::Format_ARGB32);
        shot.fill(Qt::black);
        boardView->render(&shot);
        report(!shot.isNull(), "notes-board-paints", QString());
    }
    canvas->addCommand(GigaReferenceBoard::sendToCanvas({other}));
    pump(10);
    report(!other->isOnBoard() && other->isVisible(), "notes-back-to-canvas", QString());
}

/// Diagnostic: add a note on a real document (GIGA_NOTES_FILE) and time it.
void testNotesFreeze(KisMainWindow *w)
{
    if (qEnvironmentVariable("GIGA_SELFTEST") == QLatin1String("all")) return; // diagnostic, run by name only
    KisDocument *doc = nullptr;
    const QString file = qEnvironmentVariable("GIGA_NOTES_FILE");
    if (!file.isEmpty()) {
        doc = KisPart::instance()->createDocument();
        KisPart::instance()->addDocument(doc, false);
        if (!doc->openPath(file)) { report(false, "setup", QStringLiteral("cannot open ") + file); return; }
        w->showDocument(doc);
        pump(40);
        if (doc->image()) doc->image()->waitForDone();
        pump(40);
    } else {
        // Like the user's sketch: big, 100 ppi, several layers, no reference layer.
        doc = KisPart::instance()->createDocument();
        KisPart::instance()->addDocument(doc, false);
        const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
        doc->newImage(QStringLiteral("big"), 7020, 3120, cs, KoColor(Qt::white, cs), KisConfig::RASTER_LAYER, 8,
                      QString(), 100.0 / 72.0);
        w->showDocument(doc);
        pump(20);
    }
    KisView *view = w->activeView();
    KisCanvas2 *canvas = view ? view->canvasBase() : nullptr;
    if (!canvas) { report(false, "setup", QStringLiteral("no canvas")); return; }
    view->zoomManager()->slotZoomToFit();
    pump(20);

    if (qEnvironmentVariableIsSet("GIGA_NOTES_PLACE_FIRST")) {
        KoToolManager::instance()->switchToolRequested(QStringLiteral("ToolReferenceImages"));
        pump(10);
        KoToolBase *tool0 = KoToolManager::instance()->toolById(canvas, QStringLiteral("ToolReferenceImages"));
        QMetaObject::invokeMethod(tool0, "gigaAddNote", Qt::DirectConnection);
        pump(3);
        QWidget *cw0 = canvas->canvasWidget();
        const QPointF p(cw0->width() * 0.5, cw0->height() * 0.5);
        QElapsedTimer t0;
        t0.start();
        QMouseEvent press(QEvent::MouseButtonPress, p, cw0->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(cw0, &press);
        info(QStringLiteral("first place press %1 ms").arg(t0.restart()));
        for (int i = 0; i < 10; ++i) qApp->processEvents();
        info(QStringLiteral("after press rounds %1 ms").arg(t0.restart()));
        QMouseEvent release(QEvent::MouseButtonRelease, p, cw0->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(cw0, &release);
        for (int i = 0; i < 10; ++i) qApp->processEvents();
        info(QStringLiteral("after release %1 ms, layer %2, editor %3").arg(t0.restart())
                 .arg(doc->referenceImagesLayer() ? GigaNote::notesOf(doc->referenceImagesLayer().data()).size() : -1)
                 .arg(GigaNoteEditor::current() ? "open" : "none"));
    }

    int stateChanges = 0;
    QObject::connect(canvas, &KisCanvas2::sigCanvasStateChanged, canvas, [&stateChanges]() { ++stateChanges; });
    QElapsedTimer t;
    t.start();
    KisAction *addAction = view->viewManager()->actionManager()->actionByName(QStringLiteral("giga_add_note"));
    addAction->trigger();
    info(QStringLiteral("trigger took %1 ms").arg(t.restart()));
    if (qEnvironmentVariableIsSet("GIGA_NOTES_NO_TYPING")) {
        t.restart();
        GigaNoteEditor::finishCurrent(); // like the old focus-out: closes at once, image still busy
        for (int i = 0; i < 20; ++i) qApp->processEvents();
        info(QStringLiteral("immediate empty finish %1 ms, notes %2").arg(t.restart())
                 .arg(GigaNote::notesOf(doc->referenceImagesLayer().data()).size()));
    }
    for (int i = 0; i < 20; ++i) {
        QElapsedTimer one;
        one.start();
        qApp->processEvents();
        if (one.elapsed() > 200) info(QStringLiteral("processEvents round %1 took %2 ms").arg(i).arg(one.elapsed()));
    }
    info(QStringLiteral("20 rounds took %1 ms, state changes %2").arg(t.restart()).arg(stateChanges));
    QPointer<GigaNoteEditor> editor = GigaNoteEditor::current();
    info(QStringLiteral("editor %1").arg(editor ? "open" : "none"));
    if (editor && qEnvironmentVariableIsSet("GIGA_NOTES_NO_TYPING")) {
        t.restart();
        GigaNoteEditor::finishCurrent();
        pump(5);
        info(QStringLiteral("empty finish took %1 ms, notes %2").arg(t.elapsed())
                 .arg(GigaNote::notesOf(doc->referenceImagesLayer().data()).size()));
    } else if (editor) {
        for (const QChar c : QStringLiteral("hello")) {
            QKeyEvent press(QEvent::KeyPress, int(Qt::Key_A) + (c.unicode() - 'a'), Qt::NoModifier, QString(c));
            QElapsedTimer one;
            one.start();
            QCoreApplication::sendEvent(editor, &press);
            qApp->processEvents();
            info(QStringLiteral("key %1 took %2 ms").arg(c).arg(one.elapsed()));
        }
        t.restart();
        GigaNoteEditor::finishCurrent();
        pump(5);
        info(QStringLiteral("finish took %1 ms").arg(t.elapsed()));
    }

    // Add Note button, then a click on the canvas (mouse, then pen).
    KoToolBase *tool = KoToolManager::instance()->toolById(canvas, QStringLiteral("ToolReferenceImages"));
    QWidget *cw = canvas->canvasWidget();
    for (int pass = 0; pass < 2 && tool; ++pass) {
        const bool pen = pass == 1;
        KoToolManager::instance()->switchToolRequested(QStringLiteral("ToolReferenceImages"));
        pump(5);
        QMetaObject::invokeMethod(tool, "gigaAddNote", Qt::DirectConnection);
        pump(3);
        info(QStringLiteral("active tool %1").arg(KoToolManager::instance()->activeToolId()));
        const int before = GigaNote::notesOf(doc->referenceImagesLayer().data()).size();
        const QPointF p(cw->width() * 0.4, cw->height() * 0.4);
        const QPointF g = cw->mapToGlobal(p);
        {
            QEnterEvent enter(p, p, g);
            QCoreApplication::sendEvent(cw, &enter);
            QMouseEvent move(QEvent::MouseMove, p, g, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(cw, &move);
            pump(2);
        }
        t.restart();
        if (pen) {
            QTabletEvent press(QEvent::TabletPress, QPointingDevice::primaryPointingDevice(), p, g, 0.5, 0, 0, 0, 0, 0,
                               Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
            QCoreApplication::sendEvent(cw, &press);
        } else {
            QMouseEvent press(QEvent::MouseButtonPress, p, g, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(cw, &press);
        }
        info(QStringLiteral("%1 press took %2 ms, editor %3").arg(pen ? "pen" : "mouse").arg(t.restart())
                 .arg(GigaNoteEditor::current() ? "open" : "none"));
        pump(5);
        if (pen) {
            QTabletEvent release(QEvent::TabletRelease, QPointingDevice::primaryPointingDevice(), p, g, 0, 0, 0, 0, 0, 0,
                                 Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
            QCoreApplication::sendEvent(cw, &release);
        } else {
            QMouseEvent release(QEvent::MouseButtonRelease, p, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(cw, &release);
        }
        for (int i = 0; i < 20; ++i) {
            QElapsedTimer one;
            one.start();
            qApp->processEvents();
            if (one.elapsed() > 200) info(QStringLiteral("round %1 took %2 ms").arg(i).arg(one.elapsed()));
        }
        info(QStringLiteral("%1 release+20 rounds %2 ms, notes %3 -> %4, editor %5")
                 .arg(pen ? "pen" : "mouse").arg(t.restart()).arg(before)
                 .arg(GigaNote::notesOf(doc->referenceImagesLayer().data()).size())
                 .arg(GigaNoteEditor::current() ? "open" : "none"));
        GigaNoteEditor::finishCurrent();
        pump(5);
    }
    report(true, "notesfreeze-done", QString());
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
        {QStringLiteral("motion"), testMotion},
        {QStringLiteral("autosave"), testAutosaveName},
        {QStringLiteral("accent"), testAccentColor},
        {QStringLiteral("refboard"), testReferenceBoard},
        {QStringLiteral("refsearch"), testReferenceBoardSearch},
        {QStringLiteral("icons"), testToolIcons},
        {QStringLiteral("blender"), testBlenderBridge},
        {QStringLiteral("canvaslook"), testCanvasLook},
        {QStringLiteral("layerbuttons"), testLayerButtons},
        {QStringLiteral("lassofill"), testLassoFill},
        {QStringLiteral("menuletter"), testMenuLetter},
        {QStringLiteral("cursorlook"), testCursorLook},
        {QStringLiteral("laststroke"), testLastStroke},
        {QStringLiteral("blendertransform"), testBlenderTransform},
        {QStringLiteral("strokepoints"), testStrokePoints},
        {QStringLiteral("notes"), testNotes},
        {QStringLiteral("notesfreeze"), testNotesFreeze},
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
