/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaBlenderBridge.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

#include <klocalizedstring.h>
#include <kconfiggroup.h>
#include <ksharedconfig.h>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>

#include "KisDocument.h"
#include "KisMainWindow.h"
#include "KisPart.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "kis_config.h"
#include "kis_file_layer.h"
#include "kis_group_layer.h"
#include "kis_image.h"
#include "kis_layer_utils.h"
#include "kis_node_commands_adapter.h"
#include "kis_node_manager.h"
#include "kis_paint_layer.h"

namespace {
GigaBlenderBridge *s_instance = nullptr;
const int kProtocolVersion = 1;
const char *kFolderProperty = "gigaBlenderFolder";
const int kMaxLineBytes = 64 * 1024; // messages are tiny; anything bigger is garbage

QString cleanAbsolute(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

KisMainWindow *targetWindow()
{
    return KisPart::instance()->currentMainwindow();
}

/// Every File Layer of the image whose picture is @p path.
QList<KisFileLayer*> fileLayersShowing(KisImageSP image, const QString &path)
{
    QList<KisFileLayer*> result;
    if (!image) return result;
    const QString wanted = cleanAbsolute(path);
    KisLayerUtils::recursiveApplyNodes(image->root(), [&](KisNodeSP node) {
        if (KisFileLayer *fl = qobject_cast<KisFileLayer*>(node.data())) {
            if (cleanAbsolute(fl->path()) == wanted) result << fl;
        }
    });
    return result;
}
}

void GigaBlenderBridge::ensureStarted(KisMainWindow *window)
{
    if (!s_instance) {
        s_instance = new GigaBlenderBridge(qApp);
        s_instance->listen();
    }
    if (window && !s_instance->m_windows.contains(window)) {
        s_instance->m_windows << window;
        connect(window, &KisMainWindow::activeViewChanged, s_instance, &GigaBlenderBridge::slotActiveViewChanged);
    }
}

GigaBlenderBridge *GigaBlenderBridge::instance()
{
    return s_instance;
}

GigaBlenderBridge::GigaBlenderBridge(QObject *parent)
    : QObject(parent)
{
    connect(qGuiApp, &QGuiApplication::applicationStateChanged,
            this, &GigaBlenderBridge::slotApplicationStateChanged);
}

QString GigaBlenderBridge::socketPath() const
{
    const QString env = qEnvironmentVariable("GIGA_BLENDER_SOCKET");
    if (!env.isEmpty()) return env;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    return QDir(dir).filePath(QStringLiteral("gigasketch-blender.sock"));
}

int GigaBlenderBridge::clientCount() const
{
    return m_clients.size();
}

bool GigaBlenderBridge::refreshOnFocusEnabled()
{
    return KisConfig(true).readEntry<bool>("gigaBlenderRefreshOnFocus", true);
}

bool GigaBlenderBridge::listen()
{
    const QString path = socketPath();

    // Another Giga Sketch already answers there: leave it alone (Blender then
    // talks to that one). Otherwise the file is left over from a crash.
    {
        QLocalSocket probe;
        probe.connectToServer(path);
        if (probe.waitForConnected(100)) {
            qInfo() << "Giga Blender bridge: another Giga Sketch is listening on" << path;
            return false;
        }
    }
    QLocalServer::removeServer(path);

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption); // only this user can connect
    connect(m_server, &QLocalServer::newConnection, this, &GigaBlenderBridge::slotNewConnection);
    if (!m_server->listen(path)) {
        qWarning() << "Giga Blender bridge: cannot listen on" << path << m_server->errorString();
        return false;
    }
    return true;
}

void GigaBlenderBridge::slotNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        m_clients << socket;
        connect(socket, &QLocalSocket::readyRead, this, &GigaBlenderBridge::slotReadyRead);
        connect(socket, &QLocalSocket::disconnected, this, &GigaBlenderBridge::slotDisconnected);
    }
}

void GigaBlenderBridge::slotDisconnected()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket*>(sender());
    m_clients.removeAll(socket);
    if (socket) socket->deleteLater();
}

void GigaBlenderBridge::slotReadyRead()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket*>(sender());
    if (!socket) return;
    while (socket->canReadLine()) {
        const QByteArray line = socket->readLine(kMaxLineBytes).trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
        const QJsonObject msg = doc.object();
        const QString type = msg.value("type").toString();

        if (type == QLatin1String("hello")) {
            socket->setProperty(kFolderProperty, cleanAbsolute(msg.value("folder").toString()));
            sendCanvasSize(socket);
        } else {
            const QJsonObject reply = handleMessage(msg);
            if (!reply.isEmpty()) send(socket, reply);
        }
    }
    if (socket->bytesAvailable() > kMaxLineBytes) socket->readAll(); // no newline in sight: drop it
}

void GigaBlenderBridge::send(QLocalSocket *socket, const QJsonObject &msg)
{
    QJsonObject m = msg;
    m.insert("version", kProtocolVersion);
    socket->write(QJsonDocument(m).toJson(QJsonDocument::Compact) + '\n');
}

QJsonObject GigaBlenderBridge::canvasMessage(const QString &type) const
{
    QJsonObject m;
    m.insert("type", type);
    KisMainWindow *w = targetWindow();
    KisView *view = w ? w->activeView() : nullptr;
    if (view && view->image()) {
        m.insert("width", view->image()->width());
        m.insert("height", view->image()->height());
    }
    return m;
}

void GigaBlenderBridge::sendCanvasSize(QLocalSocket *socket)
{
    send(socket, canvasMessage(QStringLiteral("canvas")));
}

void GigaBlenderBridge::slotActiveViewChanged()
{
    for (QLocalSocket *s : std::as_const(m_clients)) sendCanvasSize(s);
}

bool GigaBlenderBridge::activeDocumentHasBlenderLayer() const
{
    KisMainWindow *w = targetWindow();
    KisView *view = w ? w->activeView() : nullptr;
    if (!view || !view->image()) return false;

    QStringList folders;
    for (QLocalSocket *s : m_clients) {
        const QString f = s->property(kFolderProperty).toString();
        if (!f.isEmpty()) folders << f + QLatin1Char('/');
    }
    bool found = false;
    KisLayerUtils::recursiveApplyNodes(view->image()->root(), [&](KisNodeSP node) {
        if (KisFileLayer *fl = qobject_cast<KisFileLayer*>(node.data())) {
            const QString p = cleanAbsolute(fl->path());
            for (const QString &f : std::as_const(folders)) {
                if (p.startsWith(f)) found = true;
            }
        }
    });
    return found;
}

int GigaBlenderBridge::requestRefresh()
{
    if (m_clients.isEmpty() || !activeDocumentHasBlenderLayer()) return 0;
    const QJsonObject msg = canvasMessage(QStringLiteral("refresh"));
    for (QLocalSocket *s : std::as_const(m_clients)) send(s, msg);
    return m_clients.size();
}

void GigaBlenderBridge::slotApplicationStateChanged(Qt::ApplicationState state)
{
    if (state != Qt::ApplicationActive || !refreshOnFocusEnabled()) return;
    // Alt+Tab often flips the state several times; one request per second is plenty.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastRefreshMs < 1000) return;
    m_lastRefreshMs = now;
    requestRefresh();
}

QJsonObject GigaBlenderBridge::handleMessage(const QJsonObject &msg)
{
    QJsonObject ack;
    ack.insert("type", "ack");
    ack.insert("result", "ignored");
    if (msg.value("type").toString() != QLatin1String("frame")) return QJsonObject();

    const QString path = msg.value("path").toString();
    const bool explicitSend = msg.value("reason").toString() != QLatin1String("refresh");
    if (path.isEmpty() || !QFileInfo(path).isAbsolute() || !QFileInfo::exists(path)) {
        ack.insert("error", "file not found");
        return ack;
    }

    KisMainWindow *w = targetWindow();
    if (!w && !m_windows.isEmpty()) w = m_windows.first();
    if (!w) return ack;
    KisView *view = w->activeView();

    // 1. Layers that already show this picture: reload them now (the file
    //    watcher would also do it, but only after its 500 ms pause).
    if (view && view->image()) {
        const QList<KisFileLayer*> layers = fileLayersShowing(view->image(), path);
        if (!layers.isEmpty()) {
            for (KisFileLayer *fl : layers) fl->resetCache();
            if (explicitSend) {
                view->viewManager()->showFloatingMessage(
                    i18n("Blender: updated “%1”", layers.first()->name()), QIcon(), 1500);
            }
            ack.insert("result", "updated");
            ack.insert("layer", layers.first()->name());
            return ack;
        }
    }
    if (!explicitSend) return ack; // a refresh never adds layers

    // 2. No document open: make one at the frame's size.
    bool newDocument = false;
    if (!view || !view->image()) {
        const int width = qBound(1, msg.value("width").toInt(1920), 16384);
        const int height = qBound(1, msg.value("height").toInt(1080), 16384);
        KisDocument *doc = KisPart::instance()->createDocument();
        KisPart::instance()->addDocument(doc, false);
        const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
        if (!doc->newImage(i18n("Blender sketch"), width, height, cs, KoColor(Qt::white, cs),
                           KisConfig::RASTER_LAYER, 1, QString(), 300.0 / 72.0)) {
            ack.insert("error", "could not create a document");
            return ack;
        }
        w->showDocument(doc);
        view = w->activeView();
        if (!view || !view->image()) return ack;
        newDocument = true;
    }

    // 3. Add a File Layer above the active layer (inside its group).
    KisViewManager *vm = view->viewManager();
    KisImageSP image = view->image();
    const QString name = msg.value("name").toString().left(200).isEmpty()
            ? i18n("Blender") : msg.value("name").toString().left(200);
    KisNodeSP active = vm->activeNode();
    while (active && !active->inherits("KisLayer")) active = active->parent(); // a mask: use its layer
    KisNodeSP parent = active ? active->parent() : KisNodeSP();
    KisNodeSP above = active;
    if (!parent) { parent = image->root(); above = image->root()->lastChild(); } // nothing active: on top

    KisFileLayer *fileLayer = new KisFileLayer(image, QString(), cleanAbsolute(path),
                                               KisFileLayer::ToImageSize, QStringLiteral("Bicubic"),
                                               name, OPACITY_OPAQUE_U8);
    KisNodeCommandsAdapter adapter(vm);
    adapter.addNode(fileLayer, parent, above);

    if (newDocument) {
        // A fresh document: give the user an empty layer on top to paint on.
        KisPaintLayerSP paint = new KisPaintLayer(image, i18n("Paint"), OPACITY_OPAQUE_U8);
        adapter.addNode(paint, image->root(), fileLayer);
        vm->nodeManager()->slotNonUiActivatedNode(paint);
    } else if (active) {
        // Keep painting where you were; the Blender layer only appears.
        vm->nodeManager()->slotNonUiActivatedNode(active);
    }
    vm->showFloatingMessage(i18n("Blender: added layer “%1”", name), QIcon(), 2500);
    ack.insert("result", "created");
    ack.insert("layer", name);
    return ack;
}
