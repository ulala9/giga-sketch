/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GIGA_CANVAS_SHADOW_H
#define GIGA_CANVAS_SHADOW_H

#include <QPolygonF>
#include <QTransform>
#include <QVector>
#include <QtMath>

/**
 * Giga Sketch: soft shadow around the image, so it looks like a sheet lying on
 * the dark surround.
 *
 * The shadow is a stack of thin "rings" around the image: ring i is the image
 * rectangle grown by (i + 1) * step pixels, minus the image itself. Every ring
 * is drawn with the same small alpha, so near the edge many rings overlap
 * (darker) and further out only the big ones do (lighter): a gradient without
 * any texture or blur pass. Only the border area is drawn, never the area under
 * the image, so the cost is a few thin triangles per frame.
 *
 * The rings grow along the image's own edges, so the shadow rotates and flips
 * with the canvas. They are pushed down a little on screen (light from above).
 *
 * Shared by the OpenGL canvas (KisOpenGLCanvasRenderer) and the QPainter canvas.
 */
namespace GigaCanvasShadow {

inline const char *configKey() { return "gigaCanvasShadow"; }

constexpr int ringCount = 10;
constexpr qreal ringStep = 1.4;     // widget pixels per ring -> ~14 px shadow
constexpr qreal ringAlpha = 0.06;   // per ring; ~0.46 total at the image edge
constexpr qreal dropDown = 0.3;     // screen-down shift, as a share of the ring size

/// The 4 image corners in widget coordinates, in order around the rectangle.
inline QPolygonF imageCorners(const QRectF &imageRect, const QTransform &imageToWidget)
{
    QPolygonF c;
    c << imageToWidget.map(imageRect.topLeft()) << imageToWidget.map(imageRect.topRight())
      << imageToWidget.map(imageRect.bottomRight()) << imageToWidget.map(imageRect.bottomLeft());
    return c;
}

/// Outer corners of ring `i` (0 = smallest), same corner order as `corners`.
inline QPolygonF ringOuter(const QPolygonF &corners, int i)
{
    const qreal e = (i + 1) * ringStep;
    const QPointF uRaw = corners[1] - corners[0];
    const QPointF vRaw = corners[3] - corners[0];
    const qreal ul = qSqrt(uRaw.x() * uRaw.x() + uRaw.y() * uRaw.y());
    const qreal vl = qSqrt(vRaw.x() * vRaw.x() + vRaw.y() * vRaw.y());
    if (ul < 1e-6 || vl < 1e-6) return QPolygonF();
    const QPointF u = uRaw / ul * e;
    const QPointF v = vRaw / vl * e;
    const QPointF down(0.0, dropDown * e);
    QPolygonF o;
    o << corners[0] - u - v + down << corners[1] + u - v + down
      << corners[2] + u + v + down << corners[3] - u + v + down;
    return o;
}

} // namespace GigaCanvasShadow

#endif
