/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef GIGA_REFERENCE_LAYOUT_H
#define GIGA_REFERENCE_LAYOUT_H

#include <QRectF>
#include <QSizeF>
#include <QVector>

#include "kritaui_export.h"

/**
 * Giga Sketch: layout maths for the Reference Board (normalize sizes,
 * arrange styles). Pure functions: a list of sizes goes in, a list of
 * sizes or rectangles comes out, in the same order. No Qt widgets, no
 * images, no threads, so it is cheap and easy to test.
 */
namespace GigaReferenceLayout
{

enum class NormalizeMode {
    SameArea = 0,   ///< every image covers about the same area (default)
    SameHeight,
    SameWidth,
    MatchSelected,  ///< every image gets the area of sizes[matchIndex]
};

enum class Style {
    JustifiedRows = 0, ///< equal-height rows, all ending at the same right edge
    Masonry,           ///< equal-width columns, each image goes in the shortest column
    Packed,            ///< sizes kept, tightest bottom-left packing
    Grid,              ///< equal cells, each image fitted (centered) in its cell
    Auto,              ///< tries all of the above, keeps the best score
};

struct Options {
    Style style = Style::JustifiedRows;
    qreal spacing = 0.0;      ///< gap between images, in the same units as the sizes
    qreal targetAspect = 1.0; ///< wanted width / height of the whole group
};

struct Result {
    QVector<QRectF> rects; ///< one per input size, group top-left at (0, 0)
    Style usedStyle = Style::JustifiedRows; ///< what Auto picked (or the style asked for)
};

/// New sizes, same aspect ratios. The target is the average of the current
/// sizes, so nothing jumps dramatically. Empty or invalid sizes are kept.
KRITAUI_EXPORT QVector<QSizeF> normalize(const QVector<QSizeF> &sizes, NormalizeMode mode, int matchIndex = -1);

/// Lays the sizes out. Every style except Packed may scale the images
/// (keeping their aspect ratios).
KRITAUI_EXPORT Result arrange(const QVector<QSizeF> &sizes, const Options &options);

/// How good a layout is: fill ratio (image area / bounding area) times how
/// close its aspect is to targetAspect. 1.0 = perfect.
KRITAUI_EXPORT qreal score(const QVector<QRectF> &rects, qreal targetAspect);

KRITAUI_EXPORT QRectF bounds(const QVector<QRectF> &rects);

/// Indices sorted the way a person reads: rows top to bottom, then left to
/// right inside a row.
KRITAUI_EXPORT QVector<int> readingOrder(const QVector<QRectF> &rects);

} // namespace GigaReferenceLayout

#endif
