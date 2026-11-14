/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaReferenceBoardPlugin.h"
#include "GigaReferenceBoardDock.h"

#include <kpluginfactory.h>

#include <KoDockFactoryBase.h>
#include <KoDockRegistry.h>

K_PLUGIN_FACTORY_WITH_JSON(GigaReferenceBoardPluginFactory, "krita_gigareferenceboard.json",
                           registerPlugin<GigaReferenceBoardPlugin>();)

namespace
{
class GigaReferenceBoardDockFactory : public KoDockFactoryBase
{
public:
    QString id() const override
    {
        return QStringLiteral("GigaReferenceBoard");
    }

    virtual Qt::DockWidgetArea defaultDockWidgetArea() const
    {
        return Qt::RightDockWidgetArea;
    }

    QDockWidget *createDockWidget() override
    {
        GigaReferenceBoardDock *dock = new GigaReferenceBoardDock();
        dock->setObjectName(id());
        return dock;
    }

    DockPosition defaultDockPosition() const override
    {
        // Hidden until opened (panel strip, Settings > Dockers, or Send to Board).
        return DockMinimized;
    }
};
} // namespace

GigaReferenceBoardPlugin::GigaReferenceBoardPlugin(QObject *parent, const QVariantList &)
    : QObject(parent)
{
    KoDockRegistry::instance()->add(new GigaReferenceBoardDockFactory());
}

GigaReferenceBoardPlugin::~GigaReferenceBoardPlugin()
{
}

#include "GigaReferenceBoardPlugin.moc"
