/*
 * SPDX-FileCopyrightText: 2026 Giga Sketch contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WG_HARMONY_H
#define WG_HARMONY_H

// Giga Sketch: color harmony math for the Harmony Wheel (WGHarmonyWheel).
//
// Works on the color selector's own "channel values" (KisVisualColorModel in
// an HSV/HSL/HSI/HSY mode): x = hue, y = saturation, z = value/lightness,
// all 0..1. Turning the hue there (instead of in plain screen RGB) keeps
// wide-gamut colors intact. Pure math, no Qt widgets, so it is unit-tested.

#include <QVector>
#include <QVector4D>
#include <QtMath>

namespace WGHarmony {

enum Mode {
    Complementary = 0,
    SplitComplementary,
    Analogous,
    Triadic,
    Square,
    Monochrome,
    ModeCount
};

/// Spread limits in degrees (only Analogous and Split-Complementary use it).
/// Analogous: angle between neighbors. Split: angle from the base to each side.
inline qreal minSpread(Mode m) { return m == SplitComplementary ? 100.0 : 5.0; }
inline qreal maxSpread(Mode m) { return m == SplitComplementary ? 175.0 : 90.0; }
inline qreal defaultSpread(Mode m) { return m == SplitComplementary ? 150.0 : 30.0; }
inline bool hasSpread(Mode m) { return m == Analogous || m == SplitComplementary; }

/// Hue wrapped into 0..1.
inline qreal wrapHue(qreal h)
{
    h = std::fmod(h, 1.0);
    return h < 0 ? h + 1.0 : h;
}

/// Hue offsets (degrees) of each color in a harmony, in swatch order.
/// The entry with offset 0 is the base color.
inline QVector<qreal> hueOffsets(Mode mode, qreal spread)
{
    switch (mode) {
    case Complementary:      return {0.0, 180.0};
    case SplitComplementary: return {0.0, spread, -spread};
    case Analogous:          return {-spread, 0.0, spread};
    case Triadic:            return {0.0, 120.0, 240.0};
    case Square:             return {0.0, 90.0, 180.0, 270.0};
    default:                 return {0.0};
    }
}

/// Index of the base color in harmony()'s result.
inline int baseIndex(Mode mode, const QVector4D &base)
{
    if (mode == Analogous) return 1;
    if (mode == Monochrome) {
        // matches the window chosen in harmony()
        const qreal step = 0.15;
        qreal low = base.z() - 2 * step;
        if (low < 0.05) low = 0.05;
        if (low + 4 * step > 1.0) low = 1.0 - 4 * step;
        int best = 0;
        for (int i = 1; i < 5; i++) {
            if (qAbs(low + i * step - base.z()) < qAbs(low + best * step - base.z())) best = i;
        }
        return best;
    }
    return 0;
}

/// The harmony colors for a base color, in swatch order.
/// Monochrome: five steps of value/lightness, same hue and saturation,
/// with the base exactly at baseIndex().
inline QVector<QVector4D> harmony(Mode mode, const QVector4D &base, qreal spread)
{
    QVector<QVector4D> out;
    if (mode == Monochrome) {
        const qreal step = 0.15;
        qreal low = base.z() - 2 * step;
        if (low < 0.05) low = 0.05;
        if (low + 4 * step > 1.0) low = 1.0 - 4 * step;
        const int b = baseIndex(mode, base);
        for (int i = 0; i < 5; i++) {
            QVector4D c = base;
            c.setZ(i == b ? base.z() : qBound(0.0, low + i * step, 1.0));
            out << c;
        }
        return out;
    }
    const QVector<qreal> offsets = hueOffsets(mode, spread);
    for (qreal deg : offsets) {
        QVector4D c = base;
        c.setX(deg == 0.0 ? base.x() : wrapHue(base.x() + deg / 360.0));
        out << c;
    }
    return out;
}

/// Shortest signed hue distance a → b in degrees (-180..180).
inline qreal hueDelta(qreal a, qreal b)
{
    qreal d = std::fmod((b - a) * 360.0, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

/// True if two colors are the same within 8-bit rounding (hue ignored for grays).
inline bool sameColor(const QVector4D &a, const QVector4D &b, qreal eps = 0.006)
{
    if (qAbs(a.y() - b.y()) > eps || qAbs(a.z() - b.z()) > eps) return false;
    if (a.y() < 0.02 || a.z() < 0.02) return true; // gray or black: hue doesn't matter
    return qAbs(hueDelta(a.x(), b.x())) <= eps * 360.0;
}

} // namespace WGHarmony

#endif // WG_HARMONY_H
