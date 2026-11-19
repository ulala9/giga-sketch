/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GIGA_STROKE_PATH_H
#define GIGA_STROKE_PATH_H

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>

#include "kritaui_export.h"
#include "GigaLastStroke.h"

/**
 * Giga Sketch (Transform Last Stroke, "Points" mode): the path of the last
 * stroke as something you can bend, like the points of a Grease Pencil line.
 *
 * The recording (GigaLastStrokeRecord) is a list of painting calls, each
 * between two "paint informations" (position, pressure, tilt, time...).
 * This class:
 *  1. Cuts every call into short straight pieces (at most ~2 image px), so
 *     even a single long straight line can bend in the middle. Splitting a
 *     line into pieces paints the same dabs: the brush's spacing carries on
 *     from one piece to the next.
 *  2. Measures where each piece end sits along the line ("s" = distance from
 *     the start, in image pixels).
 *  3. Keeps a few "vertices" (the dots you drag) at chosen distances, each
 *     with an offset (how far you moved it).
 *  4. Bends the path: every point at distance s moves by offsetAt(s), a
 *     smooth curve through the vertices' offsets (a monotone cubic, so it
 *     never overshoots: moving one dot never pushes its neighbors the other
 *     way).
 *  5. Gives back the bent painting calls, to be repainted with the stroke's
 *     own brush, pressure and random numbers.
 *
 * Multi-hand strokes: each extra copy's calls are matched with the main
 * copy's calls one by one, and the copy is assumed to be an affine map of
 * the main one (mirror, rotation, shift). Its bend is the main bend passed
 * through that map. If the copies don't fit such a map, build() refuses.
 *
 * Pure geometry, no Qt widgets, no threads: owned and used on the GUI thread.
 */
class KRITAUI_EXPORT GigaStrokePath
{
public:
    struct Vertex {
        qreal s {0.0};        ///< distance along the stroke (image px)
        QPointF offset;       ///< how far it has been moved (image px)
    };

    /// Falloff curves of proportional editing (same idea as Blender's).
    enum Falloff { Smooth = 0, Sphere, Root, Sharp, Linear, Constant };

    /**
     * A proportional move: the line within @c radius (measured *along* the
     * line) of the nearest center moves by @c delta times the falloff
     * weight (1 at a center, 0 at the radius). Added on top of the dots'
     * own bend, so the whole line inside the radius follows smoothly, not
     * just the dots.
     */
    struct Move {
        QVector<qreal> centers;   ///< s of the dragged dots
        qreal radius {50.0};      ///< image px along the line
        int falloff {Smooth};
        QPointF delta;            ///< image px
    };

    /// Everything that defines the bend (for Ctrl+Z).
    struct Shape {
        QVector<Vertex> vertices;
        QVector<Move> moves;
    };
    Shape shape() const { return Shape{m_vertices, m_moves}; }
    void setShape(const Shape &shape) { m_vertices = shape.vertices; m_moves = shape.moves; }

    void addMove(const Move &move) { m_moves << move; }
    /// The move being dragged right now (the last one added).
    Move *lastMove() { return m_moves.isEmpty() ? nullptr : &m_moves.last(); }

    /// Weight of a falloff curve at @p t = distance / radius (0..1).
    static qreal falloffWeight(int falloff, qreal t);
    /// Strongest weight of @p move at distance @p s (0 outside its radius).
    static qreal moveWeight(const Move &move, qreal s);

    /// s of every path point (piece ends), for drawing the radius preview.
    const QVector<qreal> &pathLengths() const { return m_lengths; }

    /// Read the recording. False (with a message) when it can't be edited.
    bool build(const GigaLastStrokeRecord &record, QString *error = nullptr);

    bool isValid() const { return !m_points.isEmpty(); }
    qreal length() const { return m_points.isEmpty() ? 0.0 : m_lengths.last(); }

    /// Place the dots again, about @p spacing image px apart on straight
    /// parts and closer where the line turns. The current bend is kept
    /// (the new dots take the offsets the path has at their place; the
    /// proportional moves are folded into the dots).
    void placeVertices(qreal spacing);

    const QVector<Vertex> &vertices() const { return m_vertices; }
    void setVertices(const QVector<Vertex> &vertices) { m_vertices = vertices; }
    void setOffset(int index, const QPointF &offset);

    /// Add a dot at distance @p s (keeps the shape). Returns its index.
    int insertVertex(qreal s);

    /// Where the dot sits now (path point + its offset).
    QPointF vertexPosition(int index) const;

    /// Offset of the path at distance @p s.
    QPointF offsetAt(qreal s) const;
    /// The bent path, one point per piece end (main copy only).
    QVector<QPointF> bentPolyline() const;
    /// The path bent by another shape (e.g. the one of the last repaint);
    /// same points, in the same order, as bentPolyline().
    QVector<QPointF> bentPolyline(const Shape &shape) const;
    /// The bent path's point at distance @p s.
    QPointF bentPointAt(qreal s) const;
    /// Bounding box of the bent path (image px).
    QRectF bentBounds() const;

    /// Nearest point of the bent path to @p p: its distance s, and how far
    /// away it is (image px).
    qreal nearestS(const QPointF &p, qreal *distance = nullptr) const;

    /// True when any dot has moved.
    bool isEdited() const;

    /// The painting calls of the bent stroke (all copies).
    QVector<GigaLastStrokeRecord::Job> bentJobs() const;

private:
    struct Piece {
        GigaLastStrokeRecord::Job job;   ///< Point or Line, positions as drawn
        qreal s1 {0.0};
        qreal s2 {0.0};
    };

    QPointF basePointAt(qreal s) const;
    void addPieces(const GigaLastStrokeRecord::Job &job, int mainIndex,
                   const QVector<GigaLastStrokeRecord::Job> &mainJobs,
                   const QVector<qreal> &mainS1, const QVector<qreal> &mainS2);

    QVector<Piece> m_pieces;           ///< every copy, in recording order
    QVector<QTransform> m_linear;      ///< per copy: maps a main-copy offset to that copy
    QVector<QPointF> m_points;         ///< main copy path, piece ends
    QVector<qreal> m_lengths;          ///< s of each m_points entry
    QVector<Vertex> m_vertices;        ///< sorted by s, first at 0, last at length()
    QVector<Move> m_moves;             ///< proportional moves, in order
};

#endif // GIGA_STROKE_PATH_H
