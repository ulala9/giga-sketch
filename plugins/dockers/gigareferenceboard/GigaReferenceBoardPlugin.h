/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_REFERENCE_BOARD_PLUGIN_H
#define GIGA_REFERENCE_BOARD_PLUGIN_H

#include <QObject>
#include <QVariant>

/// Giga Sketch: registers the Reference Board panel (docker).
class GigaReferenceBoardPlugin : public QObject
{
    Q_OBJECT
public:
    GigaReferenceBoardPlugin(QObject *parent, const QVariantList &);
    ~GigaReferenceBoardPlugin() override;
};

#endif
