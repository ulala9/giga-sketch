/*
 * SPDX-FileCopyrightText: 2025 Wolthera van Hövell tot Westerflier <griffinvalley@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SVGTEXTTOOLOPTIONSDATA_H
#define SVGTEXTTOOLOPTIONSDATA_H

#include <QString>

struct SvgTextToolOptionsData
{
    bool useCurrentTextProperties = true;
    QString cssStylePresetName = QString();

    bool useVisualBidiCursor = false;

    bool pasteRichtTextByDefault = false;

    /// Giga Sketch: put every new text on its own layer, named after the
    /// text (Photoshop-style). Off = Krita's behaviour (texts share the
    /// active vector layer).
    bool newLayerPerText = true;
    bool boxText = true; ///< Giga Sketch: a drag makes a fixed-size text box

    void writeConfig(const QString &toolId);
    void loadConfig(const QString &toolId);
    void resetConfig();
};

#endif // SVGTEXTTOOLOPTIONSDATA_H
