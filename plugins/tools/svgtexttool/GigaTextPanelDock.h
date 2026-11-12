/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_TEXT_PANEL_DOCK_H
#define GIGA_TEXT_PANEL_DOCK_H

#include <QDockWidget>

#include <KoCanvasObserverBase.h>
#include <KoDockFactoryBase.h>

class GigaTextPropertyAccess;

/**
 * Giga Sketch: a docker holding the Character or Paragraph panel. It hands
 * the active canvas's resources to the panel when the view changes.
 */
class GigaTextPanelDock : public QDockWidget, public KoCanvasObserverBase
{
    Q_OBJECT
public:
    enum Kind { Character, Paragraph };
    explicit GigaTextPanelDock(Kind kind);

    QString observerName() override { return QStringLiteral("GigaTextPanelDock"); }

protected:
    void setCanvas(KoCanvasBase *canvas) override;
    void unsetCanvas() override;

private:
    GigaTextPropertyAccess *m_access;
};

/// Registered by the text tool plugin: "GigaCharacter" and "GigaParagraph".
class GigaTextPanelDockFactory : public KoDockFactoryBase
{
public:
    explicit GigaTextPanelDockFactory(GigaTextPanelDock::Kind kind) : m_kind(kind) {}
    QString id() const override;
    QDockWidget *createDockWidget() override;
    DockPosition defaultDockPosition() const override { return DockMinimized; }

private:
    GigaTextPanelDock::Kind m_kind;
};

#endif // GIGA_TEXT_PANEL_DOCK_H
