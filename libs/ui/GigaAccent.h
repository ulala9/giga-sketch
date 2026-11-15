/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_ACCENT_H
#define GIGA_ACCENT_H

#include <QColor>
#include <QString>
#include <QVector>
#include <QPair>
#include <klocalizedstring.h>

/**
 * Giga Sketch: the interface accent colour.
 *
 * The theme file (e.g. GigaDark.colors) decides the "selection" colour used for
 * highlighted items, slider fills, checked tool buttons and focus rings. The user
 * can override it with one accent colour in Preferences > General. The choice is
 * stored as "#rrggbb" in the main config (key gigaAccentColor); an empty string
 * means "use the theme's own colour". ThemeManager::slotChangePalette() applies it.
 */
namespace GigaAccent {

inline const char *configKey() { return "gigaAccentColor"; }

/// Built-in choices: display name + colour. The first entry is "theme default".
inline QVector<QPair<QString, QColor>> presets()
{
    return {
        {i18nc("accent colour", "Theme default"), QColor()},
        {i18nc("accent colour", "Blue"),   QColor(52, 120, 200)},
        {i18nc("accent colour", "Teal"),   QColor(32, 150, 140)},
        {i18nc("accent colour", "Green"),  QColor(76, 160, 80)},
        {i18nc("accent colour", "Amber"),  QColor(214, 150, 40)},
        {i18nc("accent colour", "Coral"),  QColor(222, 96, 80)},
        {i18nc("accent colour", "Pink"),   QColor(206, 82, 140)},
        {i18nc("accent colour", "Violet"), QColor(128, 96, 200)},
        {i18nc("accent colour", "Graphite"), QColor(120, 124, 132)},
    };
}

/// Text colour that reads well on top of the accent (dark text on light accents).
inline QColor textOn(const QColor &accent)
{
    // Relative luminance, roughly as the eye sees it.
    const qreal l = 0.2126 * accent.redF() + 0.7152 * accent.greenF() + 0.0722 * accent.blueF();
    return l > 0.55 ? QColor(20, 20, 20) : QColor(255, 255, 255);
}

/// Mix two colours; t = 0 gives a, t = 1 gives b.
inline QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

} // namespace GigaAccent

#endif
