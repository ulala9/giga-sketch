/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaTextPanelDock.h"

#include <QScrollArea>

#include <klocalizedstring.h>

#include <KisViewManager.h>
#include <kis_canvas2.h>

#include "GigaCharacterPanel.h"
#include "GigaParagraphPanel.h"
#include "GigaTextPropertyAccess.h"

GigaTextPanelDock::GigaTextPanelDock(Kind kind)
    : QDockWidget(kind == Character ? i18nc("@title:window", "Character") : i18nc("@title:window", "Paragraph"))
    , m_access(new GigaTextPropertyAccess(this))
{
    QWidget *panel = kind == Character ? static_cast<QWidget *>(new GigaCharacterPanel(m_access))
                                       : static_cast<QWidget *>(new GigaParagraphPanel(m_access));
    // Scrolls instead of forcing the panel column wider than it wants.
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(panel);
    setWidget(scroll);
}

void GigaTextPanelDock::setCanvas(KoCanvasBase *canvas)
{
    KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2 *>(canvas);
    if (kisCanvas && kisCanvas->viewManager()) {
        m_access->setProviders(kisCanvas->viewManager()->canvasResourceProvider(), kisCanvas->resourceManager());
    } else {
        m_access->setProviders(nullptr, nullptr);
    }
}

void GigaTextPanelDock::unsetCanvas()
{
    m_access->setProviders(nullptr, nullptr);
}

QString GigaTextPanelDockFactory::id() const
{
    return m_kind == GigaTextPanelDock::Character ? QStringLiteral("GigaCharacter") : QStringLiteral("GigaParagraph");
}

QDockWidget *GigaTextPanelDockFactory::createDockWidget()
{
    GigaTextPanelDock *dock = new GigaTextPanelDock(m_kind);
    dock->setObjectName(id());
    return dock;
}
