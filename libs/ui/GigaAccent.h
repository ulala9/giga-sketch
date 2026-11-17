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
 * Giga Sketch: the interface accent color.
 *
 * The theme file (e.g. GigaDark.colors) decides the "selection" color used for
 * highlighted items, slider fills, checked tool buttons and focus rings. The user
 * can override it with one accent color in Preferences > General. The choice is
 * stored as "#rrggbb" in the main config (key gigaAccentColor); an empty string
 * means "use the theme's own color". ThemeManager::slotChangePalette() applies it.
 */
namespace GigaAccent {

inline const char *configKey() { return "gigaAccentColor"; }

/// Built-in choices: display name + color. The first entry is "theme default".
inline QVector<QPair<QString, QColor>> presets()
{
    return {
        {i18nc("accent color", "Theme default"), QColor()},
        {i18nc("accent color", "Blue"),   QColor(52, 120, 200)},
        {i18nc("accent color", "Teal"),   QColor(32, 150, 140)},
        {i18nc("accent color", "Green"),  QColor(76, 160, 80)},
        {i18nc("accent color", "Amber"),  QColor(214, 150, 40)},
        {i18nc("accent color", "Coral"),  QColor(222, 96, 80)},
        {i18nc("accent color", "Pink"),   QColor(206, 82, 140)},
        {i18nc("accent color", "Violet"), QColor(128, 96, 200)},
        {i18nc("accent color", "Graphite"), QColor(120, 124, 132)},
    };
}

/// Text color that reads well on top of the accent (dark text on light accents).
inline QColor textOn(const QColor &accent)
{
    // Relative luminance, roughly as the eye sees it.
    const qreal l = 0.2126 * accent.redF() + 0.7152 * accent.greenF() + 0.0722 * accent.blueF();
    return l > 0.55 ? QColor(20, 20, 20) : QColor(255, 255, 255);
}

/// Mix two colors; t = 0 gives a, t = 1 gives b.
inline QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

} // namespace GigaAccent

#endif
