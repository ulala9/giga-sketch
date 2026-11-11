/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KIS_CURVE_SMOOTHER_H
#define KIS_CURVE_SMOOTHER_H

#include <QPointF>
#include <QPolygonF>
#include <QVector>

#include <brushengine/kis_paint_information.h>
#include <kritaui_export.h>

/**
 * Giga Sketch "Smooth Curve" brush smoothing.
 *
 * Turns raw pen samples into a chain of smooth cubic Bézier segments. Two
 * stages:
 *
 * 1. A speed-adaptive low-pass filter (the "1€ filter", Casiez, Roussel and
 *    Vogel, CHI 2012). Each new pen position pulls the filtered position
 *    toward it. When the pen moves slowly, the pull is weak, so hand wobble
 *    is removed. When the pen moves fast, the pull is strong, so flicks stay
 *    crisp and don't lag. A light touch can also add smoothing.
 *
 * 2. A centripetal Catmull-Rom spline through the filtered points. Each
 *    segment between two points is turned into one cubic Bézier, which the
 *    brush engine already knows how to stamp along. "Centripetal" means the
 *    curve does not overshoot or form loops at tight turns. Very sharp turns
 *    are kept as corners instead of being rounded.
 *
 * The filtered line trails a little behind the pen. Catch-up closes that gap:
 *  - while the pen rests: catchUpStep(), driven by a timer in the helper;
 *  - when the pen lifts: either instantly with end(), or animated over time
 *    with startTail() + tailStep(), which glide the line into the pen-up
 *    point with an ease-out.
 *
 * This class only does math. It has no Qt widgets or timers and is not
 * thread-safe: KisToolFreehandHelper owns one and calls it on the GUI thread.
 */
class KRITAUI_EXPORT KisCurveSmoother
{
public:
    struct Params {
        qreal amount = 0.5;            ///< 0..1, overall smoothing strength
        qreal speedInfluence = 0.5;    ///< 0..1, how much speed reduces smoothing
        qreal pressureInfluence = 0.0; ///< 0..1, how much a light touch adds smoothing
        qreal zoom = 1.0;              ///< screen pixels per image pixel
    };

    /// One finished piece of the stroke: a cubic Bézier from p1 to p2.
    struct Segment {
        KisPaintInformation p1;
        QPointF control1;
        QPointF control2;
        KisPaintInformation p2;
    };

    KisCurveSmoother();

    /// Start a new stroke at the pen-down sample.
    void begin(const KisPaintInformation &first, const Params &params);

    /// Feed one raw pen sample. Appends any finished segments to @p out.
    void addSample(const KisPaintInformation &raw, QVector<Segment> &out);

    /**
     * The pen is resting: pull the line one step toward the last pen
     * position, as if the pen sent the same sample again at @p timeMs.
     * Appends any finished segments to @p out.
     * @return true while the line is still visibly behind the pen.
     */
    bool catchUpStep(int timeMs, QVector<Segment> &out);

    /**
     * The pen lifted. Appends all remaining segments to @p out and ends the
     * stroke. With @p catchUp the line reaches the pen-up position right
     * away; without it, the line stops where the filter currently is.
     * Also finishes a running tail animation instantly.
     */
    void end(bool catchUp, QVector<Segment> &out);

    /**
     * The pen lifted: start gliding the line into the pen-up position over
     * @p durationMs, with an ease-out (fast at first, slowing as it lands).
     * Call tailStep() regularly until it returns true.
     */
    void startTail(int nowMs, int durationMs);

    /**
     * Advance the tail animation to @p nowMs. Appends segments to @p out.
     * @return true when the tail has landed and the stroke is finished.
     */
    bool tailStep(int nowMs, QVector<Segment> &out);

    bool hasTail() const { return m_tailActive; }

    /// The last real pen sample.
    KisPaintInformation lastRawInfo() const { return m_lastRaw; }

    /// The most recent filtered sample (what the line has reached).
    KisPaintInformation lastSmoothedInfo() const;

    /**
     * The part of the line that isn't painted yet, in image coordinates:
     * from the end of the last painted segment, through the pending curve
     * points, to the pen. Used to draw the optional lag line preview.
     */
    QPolygonF pendingPath() const;

    /// Distance in screen pixels between the line's head and the pen.
    qreal lagInScreenPixels() const;

    bool isActive() const { return m_active; }

private:
    void filterTowards(const KisPaintInformation &raw, const QPointF &previousRawPos,
                       qreal dtSeconds, qreal minimumCutoff);
    void pushPoint(const KisPaintInformation &info, QVector<Segment> &out, bool force);
    void emitSegment(QVector<Segment> &out) const;
    void closeCurve(QVector<Segment> &out);

    qreal minCutoff() const;
    qreal beta() const;

private:
    Params m_params;
    bool m_active {false};

    KisPaintInformation m_lastRaw;      ///< last real pen sample
    KisPaintInformation m_filtered;     ///< current filtered sample
    QPointF m_filteredVelocity;         ///< pen velocity, image px / second
    qreal m_lastTime {0.0};             ///< ms, time of the last filter step

    // Tail animation (pen lifted)
    bool m_tailActive {false};
    QPointF m_tailStart;
    int m_tailStartTime {0};
    int m_tailDuration {0};

    /// Sliding window of the last (up to) 4 spline points. A segment is
    /// emitted between window[1] and window[2] once there are 4 points.
    QVector<KisPaintInformation> m_window;
};

#endif // KIS_CURVE_SMOOTHER_H
