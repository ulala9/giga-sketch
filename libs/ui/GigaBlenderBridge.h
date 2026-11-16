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

#include "kritaui_export.h"

class QLocalServer;
class QLocalSocket;
class KisMainWindow;

/**
 * Giga Sketch: Blender bridge (feature/blender-bridge).
 *
 * The "Send to GigaSketch" Blender add-on (giga-tools/blender/) renders the
 * viewport into a PNG and then tells us about it over a local socket
 * ($XDG_RUNTIME_DIR/gigasketch-blender.sock, or $GIGA_BLENDER_SOCKET).
 * Messages are one JSON object per line, all with "version": 1.
 *
 *   Blender -> us:  {"type":"hello", ...}
 *                   {"type":"frame","path":..,"width":..,"height":..,"name":..,"reason":"send"|"refresh"}
 *   us -> Blender:  {"type":"canvas","width":..,"height":..}   (on hello and when the active view changes)
 *                   {"type":"refresh","width":..,"height":..}  (when Giga Sketch becomes the active app)
 *                   {"type":"ack","result":"created"|"updated"|"ignored","layer":..}
 *
 * A "frame" updates every File Layer of the active document that shows that
 * PNG. If none does and the reason is "send", a File Layer is added above the
 * active layer (a new document at the frame's size if none is open).
 *
 * Everything runs in the GUI thread and never waits: the socket is
 * asynchronous and the File Layer loads its picture in the background.
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

    /// Handle one message as if Blender had sent it (also used by the self-test).
    QJsonObject handleMessage(const QJsonObject &msg);

    /// Ask every connected Blender for a fresh capture (only if the active
    /// document has a Blender layer). Returns how many were asked.
    int requestRefresh();

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

    QLocalServer *m_server {nullptr};
    QList<QLocalSocket*> m_clients;
    QList<QPointer<KisMainWindow>> m_windows;
    qint64 m_lastRefreshMs {0};
};

#endif
