/*
 * This file is part of Krita
 *
 *  SPDX-FileCopyrightText: 2006 Cyrille Berger <cberger@cberger.net>
 *  SPDX-FileCopyrightText: 2014 Sven Langkamp <sven.langkamp@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_grid_decoration.h"

#include <QPainter>
#include <QPen>
#include <QtMath>
#include <QPainterPath>
#include <QPolygonF>
#include <klocalizedstring.h>

#include <KoUnit.h>

#include "kis_grid_config.h"
#include "kis_coordinates_converter.h"
#include "kis_config.h"

namespace {
// Giga Sketch: used only when "Grid over background" is on. Draws a family of
// parallel lines  y = c + slope * (x - anchorX),  c = c0 + k * step,  for every
// k whose line crosses `area`. Line k gets the main pen when
// |index0 + k| % subdivision == 0 (the same rule Krita uses), so lines
// inside the image land exactly where Krita's own loop would put them.
void gigaDrawLineFamily(QPainter &gc, const QRectF &area,
                        qreal anchorX, qreal c0, qreal step, qreal slope,
                        int index0, int subdivision,
                        const QPen &mainPen, const QPen &subdivisionPen)
{
    if (step <= 0.0 || area.isEmpty()) return;
    subdivision = qMax(1, subdivision);

    // c = y - slope * (x - anchorX); find its range over the rect corners
    const QPointF corners[4] = {area.topLeft(), area.topRight(), area.bottomLeft(), area.bottomRight()};
    qreal cMin = 0.0, cMax = 0.0;
    for (int i = 0; i < 4; i++) {
        const qreal c = corners[i].y() - slope * (corners[i].x() - anchorX);
        cMin = (i == 0) ? c : qMin(cMin, c);
        cMax = (i == 0) ? c : qMax(cMax, c);
    }
    const qreal kMin = qCeil((cMin - c0) / step);
    const qreal kMax = qFloor((cMax - c0) / step);
    // Far zoomed out the lines would be closer than a pixel: draw nothing
    // rather than thousands of lines (keeps panning smooth).
    if (kMax - kMin > 4000) return;

    for (qreal k = kMin; k <= kMax; k += 1.0) {
        const qreal c = c0 + k * step;
        const int index = index0 + int(k);
        gc.setPen(qAbs(index) % subdivision == 0 ? mainPen : subdivisionPen);
        gc.drawLine(QPointF(area.left(), c + slope * (area.left() - anchorX)),
                    QPointF(area.right(), c + slope * (area.right() - anchorX)));
    }
}

// Giga Sketch: "Background only" grid. Square cells of a fixed size in
// screen pixels, drawn in widget coordinates on the area around the image
// only. It is anchored to the image origin, so it moves when you pan, but it
// does not zoom or rotate with the canvas.
void gigaDrawBackgroundGrid(QPainter &gc, const QRectF &updateArea,
                            const KisCoordinatesConverter *converter,
                            const KisGridConfig &config, int cellSize)
{
    const QRectF area = converter->documentToWidget(updateArea);
    if (area.isEmpty() || cellSize < 1) return;

    // Mask out the image (a polygon, so a rotated canvas works too).
    const QRectF imageRect = converter->imageRectInImagePixels();
    const QPolygonF imagePolygon = converter->imageToWidgetTransform().map(imageRect);
    QPainterPath clip;
    clip.addRect(area);
    QPainterPath imagePath;
    imagePath.addPolygon(imagePolygon);
    imagePath.closeSubpath();
    clip = clip.subtracted(imagePath);

    gc.save();
    gc.resetTransform();
    gc.setClipPath(clip, Qt::IntersectClip);
    gc.setRenderHints(QPainter::Antialiasing, false);

    // Subdivision lines only when they stay at least 4 px apart.
    int subdivision = qMax(1, int(config.subdivision()));
    while (subdivision > 1 && cellSize / subdivision < 4) subdivision--;
    const qreal step = qreal(cellSize) / subdivision;
    const QPen mainPen = config.penMain();
    const QPen subdivisionPen = config.penSubdivision();

    const QPointF origin = converter->imageToWidgetTransform().map(QPointF(0, 0));

    const int firstX = qCeil((area.left() - origin.x()) / step);
    const int lastX = qFloor((area.right() - origin.x()) / step);
    for (int i = firstX; i <= lastX; i++) {
        const qreal x = qRound(origin.x() + i * step) + 0.5;
        gc.setPen(i % subdivision == 0 ? mainPen : subdivisionPen);
        gc.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    const int firstY = qCeil((area.top() - origin.y()) / step);
    const int lastY = qFloor((area.bottom() - origin.y()) / step);
    for (int i = firstY; i <= lastY; i++) {
        const qreal y = qRound(origin.y() + i * step) + 0.5;
        gc.setPen(i % subdivision == 0 ? mainPen : subdivisionPen);
        gc.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }
    gc.restore();
}
}

struct KisGridDecoration::Private
{
    KisGridConfig config;
};

KisGridDecoration::KisGridDecoration(KisView* parent)
    : KisCanvasDecoration("grid", parent),
      m_d(new Private)
{
    setPriority(0);
}

KisGridDecoration::~KisGridDecoration()
{

}

void KisGridDecoration::setGridConfig(const KisGridConfig &config)
{
    m_d->config = config;
}

void KisGridDecoration::drawDecoration(QPainter& gc, const QRectF& updateArea, const KisCoordinatesConverter* converter, KisCanvas2* canvas)
{
    if (!m_d->config.showGrid()) return;

    Q_UNUSED(canvas);

    // Giga Sketch: "Background only" has its own screen-fixed grid.
    const KisConfig gigaCfg(true);
    if (gigaCfg.gigaGridArea() == KisConfig::GigaGridBackgroundOnly) {
        gigaDrawBackgroundGrid(gc, updateArea, converter, m_d->config, gigaCfg.gigaBackgroundGridSize());
        return;
    }

    QTransform transform = converter->imageToWidgetTransform();

    const qreal scale = KoUnit::approxTransformScale(transform);
    const int minWidgetSize = 3;
    const int effectiveSize = qMin(m_d->config.spacing().x(), m_d->config.spacing().y());

    int scaleCoeff = 1;
    quint32 subdivision = m_d->config.subdivision();

    while (qFloor(scale * scaleCoeff * effectiveSize) <= minWidgetSize) {
        if (subdivision > 1) {
            scaleCoeff = subdivision;
            subdivision = 1;
        } else {
            scaleCoeff *= 2;
        }

        if (scaleCoeff > 32768) {
            qWarning() << "WARNING: Grid Scale Coeff is too high! That is surely a bug!";
            return;
        }
    }

    QPen mainPen = m_d->config.penMain();
    QPen subdivisionPen = m_d->config.penSubdivision();

    gc.save();
    gc.setTransform(transform);

    const QRect imageRectInImagePixels = converter->imageRectInImagePixels();

    // Giga Sketch: "Grid over background" lets the grid continue past the
    // image edges over the area around the canvas. Off = Krita's behaviour.
    const bool gigaOverBackground = gigaCfg.gigaGridArea() == KisConfig::GigaGridCanvasAndBackground;
    const QRectF updateRectInImagePixels = gigaOverBackground
        ? converter->documentToImage(updateArea)
        : converter->documentToImage(updateArea) & imageRectInImagePixels;

    // for angles. This will later be a combobox to select different types of options
    // also add options to hide specific lines (vertical, horizontal, angle 1, etc
    KisGridConfig::GridType gridType = m_d->config.gridType();

    if (gridType == KisGridConfig::GRID_RECTANGULAR) {
        gc.setRenderHints(QPainter::Antialiasing, false);
        qreal x1, y1, x2, y2;
        updateRectInImagePixels.getCoords(&x1, &y1, &x2, &y2);

        // compensate the fact the getCoordt returns off-by-one pixel
        // at the bottom right of the rect.
        x2++;
        y2++;

        if (m_d->config.xSpacingActive()) {
            // vertical lines
            int offset = 0;
            if (m_d->config.offsetActive()) {
                offset = m_d->config.offset().x();
            }
            const int step = scaleCoeff * m_d->config.spacing().x();
            const int lineIndexFirst = qCeil((x1 - offset) / step);
            const int lineIndexLast = qFloor((x2 - offset) / step);

            if (mainPen.style() != Qt::SolidLine) {
                mainPen.setDashOffset(y1 * scale);
            }
            if (subdivisionPen.style() != Qt::SolidLine) {
                subdivisionPen.setDashOffset(y1 * scale);
            }

            for (int i = lineIndexFirst; i <= lineIndexLast; i++) {
                int w = offset + i * step;

                gc.setPen(i % subdivision == 0 ? mainPen : subdivisionPen);
                // we adjusted y2 to draw the grid correctly, clip it now...
                gc.drawLine(QPointF(w, y1),QPointF(w, gigaOverBackground ? y2 : qMin(y2, qreal(imageRectInImagePixels.bottom() + 1))));
            }
        }

        if (m_d->config.ySpacingActive()) {
            // horizontal lines
            int offset = 0;
            if (m_d->config.offsetActive()) {
                offset = m_d->config.offset().y();
            }
            const int step = scaleCoeff * m_d->config.spacing().y();
            const int lineIndexFirst = qCeil((y1 - offset) / step);
            const int lineIndexLast = qFloor((y2 - offset) / step);

            if (mainPen.style() != Qt::SolidLine) {
                mainPen.setDashOffset(x1 * scale);
            }
            if (subdivisionPen.style() != Qt::SolidLine) {
                subdivisionPen.setDashOffset(x1 * scale);
            }

            for (int i = lineIndexFirst; i <= lineIndexLast; i++) {
                int w = offset + i * step;

                gc.setPen(i % subdivision == 0 ? mainPen : subdivisionPen);
                // we adjusted x2 to draw the grid correctly, clip it now...
                gc.drawLine(QPointF(x1, w),QPointF(gigaOverBackground ? x2 : qMin(x2, qreal(imageRectInImagePixels.right() + 1)), w));
            }
        }
    } else if (gridType == KisGridConfig::GRID_ISOMETRIC_LEGACY)  {
        qreal x1, y1, x2, y2;

        // get true coordinates, not just the updateArea
        QRectF trueImageRect = converter->imageRectInImagePixels();
        trueImageRect.getCoords(&x1, &y1, &x2, &y2);

        // compensate the fact the getCoordt returns off-by-one pixel
        // at the bottom right of the rect.
        x2++;
        y2++;

        int offset = 0;
        int offsetY = 0;
        if (m_d->config.offsetActive()) {
            offset = m_d->config.offset().x();
            offsetY = m_d->config.offset().y();
        }
        const int cellSpacing = m_d->config.cellSpacing();

        gc.setClipping(true);
        gc.setClipRect(updateRectInImagePixels, Qt::IntersectClip);

        // left angle
        if (m_d->config.angleLeftActive()) {
            const qreal gridXAngle = m_d->config.angleLeft();
            const qreal bottomRightOfImageY = y2; // this should be the height of the image
            qreal finalY = 0.0;

            // figure out the spacing based off the angle. The spacing needs to be perpendicular to the angle,
            // so we need to do a bit of trig to get the correct spacing.
            qreal correctedAngleSpacing = cellSpacing;
            if (gridXAngle > 0.0) {
                correctedAngleSpacing = cellSpacing / qCos(qDegreesToRadians(gridXAngle));
                gc.setRenderHints(QPainter::Antialiasing, true);
            } else {
                // horizontal line: don't want to be antialiased
                gc.setRenderHints(QPainter::Antialiasing, false);
            }

            qreal counter = qFloor((-(offset + offsetY)) / correctedAngleSpacing);

            if (gigaOverBackground) {
                // same lines as below: from (x1, w) to (x2, w - tan * x2)
                const qreal slope = -qTan(qDegreesToRadians(gridXAngle)) * x2 / qMax(qreal(1.0), x2 - x1);
                gigaDrawLineFamily(gc, updateRectInImagePixels, x1,
                                   counter * correctedAngleSpacing + offsetY + offset,
                                   correctedAngleSpacing, slope, 0, 1, mainPen, mainPen);
                finalY = bottomRightOfImageY; // skip Krita's image-only loop
            }

            while (finalY < bottomRightOfImageY) {

                const qreal w = (counter * correctedAngleSpacing) + offsetY + offset;
                gc.setPen(mainPen);

                // calculate where the ending point will be based off the angle
                const qreal startingY = w;
                const qreal horizontalDistance = x2;

                // qTan takes radians, so convert first before sending it
                const qreal length2 = qTan(qDegreesToRadians(gridXAngle)) * x2;

                finalY = startingY - length2;

                gc.drawLine(QPointF(x1, w), QPointF(horizontalDistance, finalY));

                counter = counter + 1.0;
            }
        }

        // right angle (almost the same thing, except starting the lines on the right side)
        if (m_d->config.angleRightActive()) {
            const qreal gridXAngle = m_d->config.angleRight(); // TODO: add another angle property
            const qreal bottomLeftOfImageY = y2;

            // figure out the spacing based off the angle
            qreal correctedAngleSpacing = cellSpacing;
            if (gridXAngle > 0.0) {
                correctedAngleSpacing = cellSpacing / qCos(qDegreesToRadians(gridXAngle));
                gc.setRenderHints(QPainter::Antialiasing, true);
            } else {
                // horizontal line: don't want to be antialiased
                gc.setRenderHints(QPainter::Antialiasing, false);
            }

            // distance is the same (width of the image)
            const qreal horizontalDistance = x2;
            // qTan takes radians, so convert first before sending it
            const qreal length2 = qTan(qDegreesToRadians(gridXAngle)) * horizontalDistance;

            // let's get x, y of the line that starts in the top right corder
            const qreal yLower = 0.0;
            const qreal yHigher = yLower - length2;

            const qreal yLeftFirst = qCeil(yHigher / correctedAngleSpacing) * correctedAngleSpacing;
            const qreal additionalOffset = yLeftFirst - yHigher;
            qreal finalY = 0.0;
            qreal counter = qFloor((-(offsetY - offset)) / correctedAngleSpacing);

            if (gigaOverBackground) {
                // same lines as below: from (x2, w) to (0, w - length2)
                const qreal slope = length2 / qMax(qreal(1.0), horizontalDistance);
                gigaDrawLineFamily(gc, updateRectInImagePixels, x2,
                                   counter * correctedAngleSpacing + offsetY - offset + additionalOffset,
                                   correctedAngleSpacing, slope, 0, 1, mainPen, mainPen);
                finalY = bottomLeftOfImageY; // skip Krita's image-only loop
            }

            while (finalY < bottomLeftOfImageY) {

                const qreal w = (counter * correctedAngleSpacing) + offsetY - offset + additionalOffset;
                gc.setPen(mainPen);

                // calculate where the ending point will be based off the angle
                const qreal startingY = w;

                finalY = startingY - length2;

                gc.drawLine(QPointF(x2, w), QPointF(0.0, finalY));

                counter = counter + 1.0;
            }
        }
    } else if (gridType == KisGridConfig::GRID_ISOMETRIC)  {
        qreal x1, y1, xRight, yBottom;

        // get true coordinates, not just the updateArea
        QRectF trueImageRect = converter->imageRectInImagePixels();
        trueImageRect.getCoords(&x1, &y1, &xRight, &yBottom);

        // compensate the fact the getCoordt returns off-by-one pixel
        // at the bottom right of the rect.
        xRight++;
        yBottom++;

        const KisGridConfig::TrigoCache trigoCache = m_d->config.trigoCache();

        int offsetX = 0;
        int offsetY = 0;
        if (m_d->config.offsetActive()) {
            offsetX = trigoCache.correctedAngleRightOffsetX;
            offsetY = m_d->config.offset().y();
        }

        gc.setClipping(true);
        gc.setClipRect(updateRectInImagePixels, Qt::IntersectClip);

        // left angle
        if (trigoCache.correctedAngleLeftCellSize > 0 && m_d->config.angleLeftActive()) {
            if (m_d->config.angleLeft() > 0.0) {
                gc.setRenderHints(QPainter::Antialiasing, true);
            } else {
                // horizontal line: don't want to be antialiased
                gc.setRenderHints(QPainter::Antialiasing, false);
            }

            const qreal length2 = trigoCache.tanAngleLeft * xRight;

            // let's get x, y of the line that starts in the top right corner
            const qreal yLower = 0.0;
            const qreal yHigher = yLower - length2;

            const qreal yLeftFirst = qCeil(yHigher / trigoCache.correctedAngleLeftCellSize) * trigoCache.correctedAngleLeftCellSize;
            const qreal additionalOffset = yLeftFirst - yHigher;

            qreal finalY = 0.0;

            // define line number used to determinate if is a subdivision or not
            int subdivisionIndex = qCeil(yHigher / trigoCache.correctedAngleLeftCellSize);

            qreal startingY = yLeftFirst + offsetY + offsetX + additionalOffset;

            if (gigaOverBackground) {
                // same lines as below: from (0, s) to (xRight, s - tan * xRight)
                gigaDrawLineFamily(gc, updateRectInImagePixels, 0.0, startingY,
                                   trigoCache.correctedAngleLeftCellSize, -trigoCache.tanAngleLeft,
                                   subdivisionIndex, subdivision, mainPen, subdivisionPen);
                finalY = yBottom; // skip Krita's image-only loop
            }

            while (finalY < yBottom) {
                // calculate where the ending point will be based off the angle
                finalY = startingY - length2;

                gc.setPen(qAbs(subdivisionIndex) % subdivision == 0 ? mainPen : subdivisionPen);
                gc.drawLine(QPointF(0.0, startingY), QPointF(xRight, finalY));

                subdivisionIndex++;
                startingY += trigoCache.correctedAngleLeftCellSize;
            }
        }

        // right angle (almost the same thing, except starting the lines on the right side)
        if (trigoCache.correctedAngleRightCellSize > 0 && m_d->config.angleRightActive()) {
            if (m_d->config.angleRight() > 0.0) {
                gc.setRenderHints(QPainter::Antialiasing, true);
            } else {
                // horizontal line: don't want to be antialiased
                gc.setRenderHints(QPainter::Antialiasing, false);
            }

            const qreal length2 = trigoCache.tanAngleRight * xRight;

            // let's get x, y of the line that starts in the top right corner
            const qreal yLower = 0.0;
            const qreal yHigher = yLower - length2;

            const qreal yLeftFirst = qCeil(yHigher / trigoCache.correctedAngleRightCellSize) * trigoCache.correctedAngleRightCellSize;
            const qreal additionalOffset = yLeftFirst - yHigher;

            qreal finalY = 0.0;

            // define line number used to determinate if is a subdivision or not
            int subdivisionIndex = qCeil(yHigher / trigoCache.correctedAngleRightCellSize);

            qreal startingY = yLeftFirst + offsetY - offsetX + additionalOffset;

            if (gigaOverBackground) {
                // same lines as below: from (xRight, s) to (0, s - tan * xRight)
                gigaDrawLineFamily(gc, updateRectInImagePixels, xRight, startingY,
                                   trigoCache.correctedAngleRightCellSize, trigoCache.tanAngleRight,
                                   subdivisionIndex, subdivision, mainPen, subdivisionPen);
                finalY = yBottom; // skip Krita's image-only loop
            }

            while (finalY < yBottom) {
                // calculate where the ending point will be based off the angle
                finalY = startingY - length2;

                gc.setPen(qAbs(subdivisionIndex) % subdivision == 0 ? mainPen : subdivisionPen);
                gc.drawLine(QPointF(xRight, startingY), QPointF(0.0, finalY));

                subdivisionIndex++;
                startingY += trigoCache.correctedAngleRightCellSize;
            }
        }

        // vertical
        if (trigoCache.verticalSpace > 0) {
            QPen verticalPen = m_d->config.penVertical();
            if (verticalPen.style() != Qt::SolidLine) {
                verticalPen.setDashOffset(updateRectInImagePixels.top() * scale);
            }
            gc.setPen(verticalPen);
            gc.setRenderHints(QPainter::Antialiasing, false);

            int offset = 0;
            if (m_d->config.offsetActive()) {
                offset = m_d->config.offset().x();
            }

            qreal pX = (xRight - x1)/(2*trigoCache.verticalSpace);
            pX = offset + (pX - qFloor(pX)) * trigoCache.verticalSpace;
            pX = pX - trigoCache.verticalSpace * qFloor(pX/trigoCache.verticalSpace);

            if (gigaOverBackground) {
                // start at the first line left of the visible area (may be < 0)
                if (pX > updateRectInImagePixels.left()) {
                    pX -= trigoCache.verticalSpace * qCeil((pX - updateRectInImagePixels.left()) / trigoCache.verticalSpace);
                }
                yBottom = updateRectInImagePixels.bottom() + 1;
            }

            while(pX <= updateRectInImagePixels.right()) {
                if (pX > updateRectInImagePixels.left()) {
                    gc.drawLine(QPointF(pX, updateRectInImagePixels.top()),QPointF(pX, qMin(yBottom, qreal(updateRectInImagePixels.bottom() + 1))));
                }
                pX += trigoCache.verticalSpace;
            }
        }
    }

    gc.restore();
}
