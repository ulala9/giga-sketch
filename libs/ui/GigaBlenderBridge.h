/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_BLENDER_BRIDGE_H
#define GIGA_BLENDER_BRIDGE_H

#include <QObject>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QHash>
#include <QTimer>
#include <QWidget>

#include <kis_types.h>

#include "kritaui_export.h"

class QLocalServer;
class QLocalSocket;
class QPropertyAnimation;
class KisMainWindow;
class GigaBlenderProgress;
class GigaBlenderProgressWidget;

/// A raw Blender picture turned into a paint device (on a worker thread).
struct GigaDecodedFrame {
    KisPaintDeviceSP device;
    QString error;
};

/**
 * Giga Sketch: Blender bridge (feature/blender-bridge).
 *
 * The "Send to GigaSketch" Blender add-on (giga-tools/blender/) renders the
 * viewport into a PNG and then tells us about it over a local socket
 * ($XDG_RUNTIME_DIR/gigasketch-blender.sock, or $GIGA_BLENDER_SOCKET).
 * Messages are one JSON object per line, all with "version": 1.
 *
 *   Blender -> us:  {"type":"hello", ...}
 *                   {"type":"rendering"} / {"type":"unchanged"} / {"type":"failed","error":..}
 *                   {"type":"frame","path":..,"width":..,"height":..,"name":..,"reason":"send"|"refresh",
 *                    "raw":"/dev/shm/gigasketch-blender-....rgba"}  (raw: optional, RGBA8 rows, top first)
 *   us -> Blender:  {"type":"canvas","width":..,"height":..}   (on hello and when the active view changes)
 *                   {"type":"refresh","width":..,"height":..}  (when Giga Sketch becomes the active app)
 *                   {"type":"ack","result":"created"|"updated"|"ignored","layer":..}
 *
 * A "frame" updates every File Layer of the active document that shows that
 * PNG. If none does and the reason is "send", a File Layer is added above the
 * active layer (a new document at the frame's size if none is open). Layers
 * we make are flagged as Blender layers (KisFileLayer::gigaIsBlenderLayer):
 * their picture is saved in the .kra too.
 *
 * Fast path: "raw" names a shared-memory file with the pixels. A worker
 * thread turns it into a paint device (and deletes the file); the GUI thread
 * only swaps it into the layer. Without "raw", the PNG is loaded (old way).
 * A small bar on the canvas (GigaBlenderProgress) says what's happening.
 *
 * Everything else runs in the GUI thread and never waits: the socket is
 * asynchronous.
 */
class KRITAUI_EXPORT GigaBlenderBridge : public QObject
{
    Q_OBJECT
public:
    /// Start listening (once per process; later calls only watch the new window).
    static void ensureStarted(KisMainWindow *window);
    static GigaBlenderBridge *instance();

    QString socketPath() const;
    int clientCount() const;

    /// Handle one message as if Blender had sent it. Returns the reply, or an
    /// empty object when there is none yet (a raw frame is acked to @p replyTo later).
    QJsonObject handleMessage(const QJsonObject &msg, QLocalSocket *replyTo = nullptr);

    GigaBlenderProgress *progress();

    /// Ask every connected Blender for a fresh capture (only if the active
    /// document has a Blender layer). Returns how many were asked.
    int requestRefresh();

    /// Layer > Refresh Blender Layers: requestRefresh() with a message saying what happened.
    void refreshFromMenu();

    static bool refreshOnFocusEnabled();

private Q_SLOTS:
    void slotNewConnection();
    void slotReadyRead();
    void slotDisconnected();
    void slotApplicationStateChanged(Qt::ApplicationState state);
    void slotActiveViewChanged();

private:
    explicit GigaBlenderBridge(QObject *parent);
    bool listen();
    void send(QLocalSocket *socket, const QJsonObject &msg);
    void sendCanvasSize(QLocalSocket *socket);
    QJsonObject canvasMessage(const QString &type) const;
    bool activeDocumentHasBlenderLayer() const;
    QJsonObject applyFrame(const QJsonObject &msg, KisPaintDeviceSP picture);
    static GigaDecodedFrame decodeRaw(const QString &raw, int width, int height);

    QLocalServer *m_server {nullptr};
    QList<QLocalSocket*> m_clients;
    QList<QPointer<KisMainWindow>> m_windows;
    qint64 m_lastRefreshMs {0};
    QHash<QString, int> m_generations; // per picture path: newest frame wins
    GigaBlenderProgress *m_progress {nullptr};
};

/**
 * The small "Blender: rendering… / updating layer… / updated" bar at the top
 * of the canvas. Mouse events go through it.
 */
class KRITAUI_EXPORT GigaBlenderProgress : public QObject
{
    Q_OBJECT
public:
    explicit GigaBlenderProgress(QObject *parent);
    ~GigaBlenderProgress() override;
    /// @p percent < 0 = busy (sliding bar). Without @p showNow the bar waits
    /// 300 ms, so quick answers never flash it.
    void stage(const QString &text, int percent, bool showNow);
    void finish(const QString &text);
    void cancel();
    QWidget *widget() const;
    QString text() const { return m_text; }

private:
    friend class GigaBlenderProgressWidget;
    void showBar();
    void fadeOut();

    QPointer<GigaBlenderProgressWidget> m_widget;
    QPropertyAnimation *m_fade {nullptr};
    QTimer m_showDelay;
    QTimer m_hideTimer;
    QTimer m_busyTimer;
    QString m_text;
    int m_percent {0};
    int m_busyPhase {0};
    bool m_active {false};
    bool m_finishing {false};
};

class GigaBlenderProgressWidget : public QWidget
{
    Q_OBJECT
public:
    explicit GigaBlenderProgressWidget(GigaBlenderProgress *owner);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    GigaBlenderProgress *m_owner;
};

#endif
