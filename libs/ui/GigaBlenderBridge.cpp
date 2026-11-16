/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaBlenderBridge.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPropertyAnimation>
#include <QtConcurrent/QtConcurrentRun>
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
#include "GigaMotion.h"
#include "kis_canvas2.h"
#include "kis_paint_device.h"
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
            const QJsonObject reply = handleMessage(msg, socket);
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
            if (fl->gigaIsBlenderLayer()) found = true;
            // Older ones (made before the flag existed): by Blender's folder.
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

void GigaBlenderBridge::refreshFromMenu()
{
    KisMainWindow *w = targetWindow();
    KisViewManager *vm = w ? w->viewManager() : nullptr;
    QString message;
    if (m_clients.isEmpty()) {
        message = i18n("Blender isn't connected: start Blender with the \u201cSend to GigaSketch\u201d add-on");
    } else if (!activeDocumentHasBlenderLayer()) {
        message = i18n("No Blender layers here: use \u201cSend to GigaSketch\u201d in Blender first");
    } else {
        requestRefresh();
        progress()->stage(i18n("Blender: checking for changes\u2026"), -1, true);
        return;
    }
    if (vm) vm->showFloatingMessage(message, QIcon(), 2500);
}

void GigaBlenderBridge::slotApplicationStateChanged(Qt::ApplicationState state)
{
    if (state != Qt::ApplicationActive || !refreshOnFocusEnabled()) return;
    // Alt+Tab often flips the state several times; one request per second is plenty.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastRefreshMs < 1000) return;
    m_lastRefreshMs = now;
    if (requestRefresh() > 0) {
        progress()->stage(i18n("Blender: checking for changes\u2026"), -1, false);
    }
}

QJsonObject GigaBlenderBridge::handleMessage(const QJsonObject &msg, QLocalSocket *replyTo)
{
    const QString type = msg.value("type").toString();
    if (type == QLatin1String("rendering")) {
        progress()->stage(i18n("Blender: rendering…"), -1, true);
        return QJsonObject();
    }
    if (type == QLatin1String("unchanged")) {
        progress()->cancel(); // nothing to show: the layer is up to date
        return QJsonObject();
    }
    if (type == QLatin1String("failed")) {
        progress()->finish(i18n("Blender: %1", msg.value("error").toString().left(200)));
        return QJsonObject();
    }
    if (type != QLatin1String("frame")) return QJsonObject();

    const QString raw = msg.value("raw").toString();
    if (raw.isEmpty()) {
        return applyFrame(msg, KisPaintDeviceSP()); // the PNG way (older add-on, or no shared memory)
    }

    // Fast way: the raw pixels in shared memory. Turning them into a paint
    // device takes a while for big canvases, so it runs on a worker thread;
    // only the final swap happens here, in the GUI thread.
    const int width = msg.value("width").toInt();
    const int height = msg.value("height").toInt();
    const QString path = cleanAbsolute(msg.value("path").toString());
    const int generation = ++m_generations[path];
    progress()->stage(i18n("Blender: updating layer…"), 70, true);

    QPointer<QLocalSocket> socket(replyTo);
    auto *watcher = new QFutureWatcher<GigaDecodedFrame>(this);
    connect(watcher, &QFutureWatcher<GigaDecodedFrame>::finished, this, [this, watcher, msg, path, generation, socket]() {
        const GigaDecodedFrame decoded = watcher->result();
        watcher->deleteLater();
        QJsonObject ack;
        if (m_generations.value(path) != generation) {
            // A newer picture of the same camera is on its way: skip this one.
            ack = QJsonObject{{"type", "ack"}, {"result", "ignored"}, {"error", "superseded"}};
        } else if (!decoded.device) {
            ack = applyFrame(msg, KisPaintDeviceSP()); // fall back to the PNG
            if (ack.value("result").toString() == QLatin1String("ignored")) {
                ack.insert("error", decoded.error);
            }
        } else {
            ack = applyFrame(msg, decoded.device);
        }
        if (socket) send(socket, ack);
    });
    watcher->setFuture(QtConcurrent::run(&GigaBlenderBridge::decodeRaw, raw, width, height));
    return QJsonObject(); // the ack is sent when the picture is in
}

GigaDecodedFrame GigaBlenderBridge::decodeRaw(const QString &raw, int width, int height)
{
    GigaDecodedFrame result;
    // Only our own shared-memory files, and only sane sizes.
    const QFileInfo info(raw);
    if (!info.isAbsolute() || !info.fileName().startsWith(QLatin1String("gigasketch-blender-"))
            || !info.fileName().endsWith(QLatin1String(".rgba"))) {
        result.error = QStringLiteral("bad raw file name");
        return result;
    }
    if (width < 1 || height < 1 || width > 16384 || height > 16384) {
        result.error = QStringLiteral("bad size");
        return result;
    }
    QFile file(raw);
    const qint64 bytes = qint64(width) * height * 4;
    if (!file.open(QIODevice::ReadOnly) || file.size() != bytes) {
        result.error = QStringLiteral("raw picture missing");
        QFile::remove(raw);
        return result;
    }
    QImage image(width, height, QImage::Format_RGBA8888); // straight alpha, top row first
    bool ok = !image.isNull() && image.bytesPerLine() == width * 4
            && file.read(reinterpret_cast<char *>(image.bits()), bytes) == bytes;
    file.close();
    QFile::remove(raw); // Blender writes a new file for every picture
    if (!ok) {
        result.error = QStringLiteral("could not read the raw picture");
        return result;
    }
    KisPaintDeviceSP device = new KisPaintDevice(KoColorSpaceRegistry::instance()->rgb8());
    device->convertFromQImage(image, nullptr);
    result.device = device;
    return result;
}

QJsonObject GigaBlenderBridge::applyFrame(const QJsonObject &msg, KisPaintDeviceSP picture)
{
    QJsonObject ack;
    ack.insert("type", "ack");
    ack.insert("result", "ignored");

    const QString path = msg.value("path").toString();
    const bool explicitSend = msg.value("reason").toString() != QLatin1String("refresh");
    if (path.isEmpty() || !QFileInfo(path).isAbsolute() || (!picture && !QFileInfo::exists(path))) {
        ack.insert("error", "file not found");
        progress()->cancel();
        return ack;
    }

    KisMainWindow *w = targetWindow();
    if (!w && !m_windows.isEmpty()) w = m_windows.first();
    if (!w) { progress()->cancel(); return ack; }
    KisView *view = w->activeView();

    // 1. Layers that already show this picture: update them.
    if (view && view->image()) {
        const QList<KisFileLayer*> layers = fileLayersShowing(view->image(), path);
        if (!layers.isEmpty()) {
            for (KisFileLayer *fl : layers) {
                fl->gigaSetBlenderLayer(true);
                if (picture) {
                    fl->gigaSetPicture(picture);
                } else {
                    // setFileName (not just a reload): the layer may not be
                    // watching the PNG (Blender layers never do).
                    fl->setFileName(QString(), cleanAbsolute(path));
                }
            }
            progress()->finish(i18n("“%1” updated", layers.first()->name()));
            ack.insert("result", "updated");
            ack.insert("layer", layers.first()->name());
            return ack;
        }
    }
    if (!explicitSend) { progress()->cancel(); return ack; } // a refresh never adds layers

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
            progress()->cancel();
            return ack;
        }
        w->showDocument(doc);
        view = w->activeView();
        if (!view || !view->image()) { progress()->cancel(); return ack; }
        newDocument = true;
    }

    // 3. Add a Blender layer (a flagged File Layer) above the active layer.
    KisViewManager *vm = view->viewManager();
    KisImageSP image = view->image();
    const QString name = msg.value("name").toString().left(200).isEmpty()
            ? i18n("Blender") : msg.value("name").toString().left(200);
    KisNodeSP active = vm->activeNode();
    while (active && !active->inherits("KisLayer")) active = active->parent(); // a mask: use its layer
    KisNodeSP parent = active ? active->parent() : KisNodeSP();
    KisNodeSP above = active;
    if (!parent) { parent = image->root(); above = image->root()->lastChild(); } // nothing active: on top

    KisFileLayer *fileLayer = nullptr;
    if (picture) {
        // No path at construction: nothing loads or watches the PNG.
        fileLayer = new KisFileLayer(image, QString(), QString(), KisFileLayer::ToImageSize,
                                     QStringLiteral("Bicubic"), name, OPACITY_OPAQUE_U8);
        fileLayer->gigaSetBlenderSource(QString(), cleanAbsolute(path));
    } else {
        fileLayer = new KisFileLayer(image, QString(), cleanAbsolute(path), KisFileLayer::ToImageSize,
                                     QStringLiteral("Bicubic"), name, OPACITY_OPAQUE_U8);
    }
    fileLayer->gigaSetBlenderLayer(true);
    KisNodeCommandsAdapter adapter(vm);
    adapter.addNode(fileLayer, parent, above);
    if (picture) fileLayer->gigaSetPicture(picture);

    if (newDocument) {
        // A fresh document: give the user an empty layer on top to paint on.
        KisPaintLayerSP paint = new KisPaintLayer(image, i18n("Paint"), OPACITY_OPAQUE_U8);
        adapter.addNode(paint, image->root(), fileLayer);
        vm->nodeManager()->slotNonUiActivatedNode(paint);
    } else if (active) {
        // Keep painting where you were; the Blender layer only appears.
        vm->nodeManager()->slotNonUiActivatedNode(active);
    }
    progress()->finish(i18n("Added layer “%1”", name));
    ack.insert("result", "created");
    ack.insert("layer", name);
    return ack;
}

GigaBlenderProgress *GigaBlenderBridge::progress()
{
    if (!m_progress) m_progress = new GigaBlenderProgress(this);
    return m_progress;
}

// ------------------------------------------------------------------ progress

GigaBlenderProgress::GigaBlenderProgress(QObject *parent)
    : QObject(parent)
{
    m_showDelay.setSingleShot(true);
    connect(&m_showDelay, &QTimer::timeout, this, [this]() { if (m_active) showBar(); });
    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, &GigaBlenderProgress::fadeOut);
    m_busyTimer.setInterval(33);
    connect(&m_busyTimer, &QTimer::timeout, this, [this]() {
        m_busyPhase = (m_busyPhase + 3) % 200;
        if (m_widget) m_widget->update();
    });
}

GigaBlenderProgress::~GigaBlenderProgress()
{
    delete m_widget.data();
}

void GigaBlenderProgress::stage(const QString &text, int percent, bool showNow)
{
    m_active = true;
    m_text = text;
    m_percent = percent;
    m_hideTimer.start(percent < 0 ? 30000 : 15000); // Blender never answered: give up quietly
    m_finishing = false;
    if (percent < 0) m_busyTimer.start(); else m_busyTimer.stop();
    if (showNow || (m_widget && m_widget->isVisible())) {
        m_showDelay.stop();
        showBar();
    } else if (!m_showDelay.isActive()) {
        m_showDelay.start(300); // quick answers ("nothing changed") never flash a bar
    }
}

void GigaBlenderProgress::finish(const QString &text)
{
    if (!m_widget || !m_widget->isVisible()) {
        // Nothing shown yet (it was quick): show the result briefly anyway.
        m_text = text;
    }
    m_active = true;
    m_text = text;
    m_percent = 100;
    m_finishing = true;
    m_busyTimer.stop();
    m_showDelay.stop();
    showBar();
    m_hideTimer.start(1200);
}

void GigaBlenderProgress::cancel()
{
    m_active = false;
    m_showDelay.stop();
    m_busyTimer.stop();
    if (m_widget && m_widget->isVisible() && !m_finishing) fadeOut();
}

QWidget *GigaBlenderProgress::widget() const
{
    return m_widget;
}

void GigaBlenderProgress::showBar()
{
    KisMainWindow *w = targetWindow();
    KisView *view = w ? w->activeView() : nullptr;
    QWidget *canvas = (view && view->canvasBase()) ? view->canvasBase()->canvasWidget() : nullptr;
    QWidget *host = canvas ? canvas : static_cast<QWidget *>(w);
    if (!host) return;

    if (!m_widget) {
        m_widget = new GigaBlenderProgressWidget(this);
        m_widget->setObjectName(QStringLiteral("GigaBlenderProgress"));
    }
    if (m_widget->parentWidget() != host) {
        m_widget->setParent(host);
    }
    if (m_fade) m_fade->stop();
    if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(m_widget->graphicsEffect())) effect->setOpacity(1.0);

    const QFontMetrics fm(m_widget->font());
    const int width = qMin(host->width() - 32, qMax(240, fm.horizontalAdvance(m_text) + 40));
    const int height = fm.height() + 22;
    m_widget->setGeometry((host->width() - width) / 2, 14, width, height);
    m_widget->show();
    m_widget->raise();
    m_widget->update();
}

void GigaBlenderProgress::fadeOut()
{
    m_active = false;
    m_busyTimer.stop();
    if (!m_widget || !m_widget->isVisible()) return;
    if (!GigaMotion::enabled()) {
        m_widget->hide();
        return;
    }
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(m_widget->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(m_widget);
        m_widget->setGraphicsEffect(effect);
    }
    if (!m_fade) {
        m_fade = new QPropertyAnimation(effect, "opacity", this);
        m_fade->setDuration(260);
        m_fade->setEndValue(0.0);
        connect(m_fade, &QPropertyAnimation::finished, this, [this]() {
            if (m_widget && !m_active) m_widget->hide();
        });
    }
    m_fade->stop();
    m_fade->setTargetObject(effect);
    m_fade->setStartValue(effect->opacity());
    m_fade->start();
}

GigaBlenderProgressWidget::GigaBlenderProgressWidget(GigaBlenderProgress *owner)
    : QWidget(nullptr)
    , m_owner(owner)
{
    setAttribute(Qt::WA_TransparentForMouseEvents); // painting goes right through it
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
}

void GigaBlenderProgressWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QColor bg = palette().color(QPalette::Window);
    bg.setAlpha(235);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.setBrush(bg);
    p.drawRoundedRect(r, 7, 7);

    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(rect().adjusted(14, 0, -14, -6), Qt::AlignCenter, m_owner->m_text);

    // The bar: a thin line along the bottom; a sliding segment while busy.
    const QRectF track(14, height() - 8, width() - 28, 3);
    QColor trackColor = palette().color(QPalette::WindowText);
    trackColor.setAlpha(45);
    p.setPen(Qt::NoPen);
    p.setBrush(trackColor);
    p.drawRoundedRect(track, 1.5, 1.5);
    p.setBrush(palette().color(QPalette::Highlight));
    if (m_owner->m_percent < 0) {
        const qreal seg = track.width() * 0.3;
        const qreal x = track.left() - seg + (track.width() + seg) * (m_owner->m_busyPhase / 200.0);
        p.setClipRect(track);
        p.drawRoundedRect(QRectF(x, track.top(), seg, track.height()), 1.5, 1.5);
    } else {
        p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * m_owner->m_percent / 100.0, track.height()), 1.5, 1.5);
    }
}
