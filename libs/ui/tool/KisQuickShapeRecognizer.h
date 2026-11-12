/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KIS_QUICK_SHAPE_RECOGNIZER_H
#define KIS_QUICK_SHAPE_RECOGNIZER_H

#include <QPainterPath>
#include <QPointF>
#include <QVector>

#include <kritaui_export.h>

/**
 * Giga Sketch "QuickShape": recognises what a hand-drawn stroke was meant
 * to be (line, arc, ellipse, rectangle, polygon, polyline), like holding
 * the pen at the end of a stroke in Procreate.
 *
 * Pure geometry: no Qt widgets, no Krita state, unit-tested in
 * libs/ui/tests/KisQuickShapeRecognizerTest.cpp.
 *
 * How it decides, in order:
 *  1. Line: no point strays more than a few % of the stroke's length from
 *     the straight line between its ends.
 *  2. Closed strokes (end returns near the start):
 *     - polygon/rectangle if simplifying the stroke leaves 3-6 sharp corners;
 *     - otherwise ellipse, fitted from the spread of the points along their
 *       two main directions, if the points sit close to it.
 *  3. Open strokes: polyline if there are a few sharp corners, otherwise an
 *     arc if the points sit close to a circle.
 * If nothing fits well enough, the result is None and the stroke stays as
 * drawn.
 */
class KRITAUI_EXPORT KisQuickShapeRecognizer
{
public:
    enum Type { None, Line, Polyline, Arc, Ellipse, Rectangle, Polygon };

    struct Result {
        Type type {None};
        /// The recognised shape, ready to be painted (lines and cubic curves).
        QPainterPath path;

        bool isValid() const { return type != None; }
    };

    /**
     * @param points  the stroke in image coordinates, in drawing order.
     * @param perfect constrain: lines snap to 15 degree steps, ellipses
     *                become circles, rectangles become squares.
     */
    static Result recognize(const QVector<QPointF> &points, bool perfect = false);

    /// Human-readable name, for messages ("Ellipse", "Line", ...).
    static QString typeName(Type type);
};

#endif // KIS_QUICK_SHAPE_RECOGNIZER_H
