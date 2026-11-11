/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisCurveSmoother.h"

#include <QtMath>

namespace {

// Tablets often report several samples with the same millisecond timestamp,
// and timestamps can jitter. Clamp the time step to a sane range so a zero or
// huge step can't make the filter jump.
constexpr qreal MIN_DT = 1.0 / 1000.0;    // seconds
constexpr qreal MAX_DT = 1.0 / 20.0;      // seconds
constexpr qreal DEFAULT_DT = 1.0 / 200.0; // used when two samples share a timestamp

// Cutoff for smoothing the speed estimate itself (standard 1€ value).
constexpr qreal DERIVATIVE_CUTOFF = 1.0; // Hz

// Range of the base cutoff, mapped from "amount" on an exponential scale.
// Lower cutoff = stronger smoothing (and more lag behind the pen).
constexpr qreal CUTOFF_AT_AMOUNT_0 = 30.0; // Hz, almost no smoothing
constexpr qreal CUTOFF_AT_AMOUNT_1 = 0.3;  // Hz, very heavy, "on a string"

// How much pen speed (in screen px/s) raises the cutoff at speed influence = 1.
constexpr qreal MAX_BETA = 0.015;

// While the pen rests or lifts, the line catches up at least this fast, so a
// heavy Amount doesn't make it crawl. ~4 Hz closes most of the gap in ~0.2 s.
constexpr qreal CATCH_UP_MIN_CUTOFF = 4.0; // Hz

// Spline points closer than this (in screen px) are skipped. Fewer, evenly
// spaced points give a cleaner curve and fewer paint jobs.
constexpr qreal MIN_POINT_SPACING = 1.5;

// While catching up, stop once the line is this close to the pen (screen px).
constexpr qreal CATCH_UP_DONE_DISTANCE = 0.5;
constexpr int MAX_CATCH_UP_STEPS = 60;
constexpr qreal CATCH_UP_DT = 1.0 / 120.0; // seconds per step for an instant catch-up

// Turns sharper than this (cosine of the turn angle) are kept as corners.
// -0.35 is about 110 degrees of direction change.
constexpr qreal CORNER_COSINE = -0.35;

constexpr qreal EPSILON = 1e-6;

// Smoothing factor of an exponential low-pass filter with the given cutoff
// frequency and time step: 0 = keep the old value, 1 = take the new value.
qreal lowPassAlpha(qreal cutoffHz, qreal dtSeconds)
{
    const qreal tau = 1.0 / (2.0 * M_PI * cutoffHz);
    return 1.0 / (1.0 + tau / dtSeconds);
}

qreal length(const QPointF &p)
{
    return std::sqrt(p.x() * p.x() + p.y() * p.y());
}

// True if the path turns sharply at `b` on the way a -> b -> c.
bool isCorner(const QPointF &a, const QPointF &b, const QPointF &c)
{
    const QPointF in = b - a;
    const QPointF out = c - b;
    const qreal lenIn = length(in);
    const qreal lenOut = length(out);
    if (lenIn < EPSILON || lenOut < EPSILON) {
        return false;
    }
    const qreal cosine = (in.x() * out.x() + in.y() * out.y()) / (lenIn * lenOut);
    return cosine < CORNER_COSINE;
}

} // namespace

KisCurveSmoother::KisCurveSmoother()
{
}

qreal KisCurveSmoother::minCutoff() const
{
    // Exponential mapping, so the slider feels even across its range.
    return CUTOFF_AT_AMOUNT_0 * std::pow(CUTOFF_AT_AMOUNT_1 / CUTOFF_AT_AMOUNT_0, m_params.amount);
}

qreal KisCurveSmoother::beta() const
{
    return MAX_BETA * m_params.speedInfluence;
}

void KisCurveSmoother::begin(const KisPaintInformation &first, const Params &params)
{
    m_params = params;
    m_params.zoom = qMax(params.zoom, EPSILON);
    m_active = true;
    m_tailActive = false;

    m_lastRaw = first;
    m_filtered = first;
    m_filteredVelocity = QPointF();
    m_lastTime = first.currentTime();

    // Start the window with the first point twice. The duplicate is a
    // "phantom" point that gives the first segment a starting direction.
    m_window.clear();
    m_window.append(first);
    m_window.append(first);
}

void KisCurveSmoother::filterTowards(const KisPaintInformation &raw, const QPointF &previousRawPos,
                                     qreal dtSeconds, qreal minimumCutoff)
{
    // 1. Estimate how fast the *pen* is moving (not how far the line lags
    //    behind it), smoothed so one noisy sample can't flip smoothing on/off.
    const QPointF rawVelocity = (raw.pos() - previousRawPos) / dtSeconds;
    const qreal velocityAlpha = lowPassAlpha(DERIVATIVE_CUTOFF, dtSeconds);
    m_filteredVelocity += velocityAlpha * (rawVelocity - m_filteredVelocity);

    // 2. Faster pen -> higher cutoff -> less smoothing. Speed is measured in
    //    screen pixels so the feel doesn't change with zoom.
    const qreal screenSpeed = length(m_filteredVelocity) * m_params.zoom;
    qreal cutoff = minCutoff() + beta() * screenSpeed;

    // 3. Optional: a light touch smooths more (factor 0.35x at no pressure,
    //    1.4x at full pressure, blended in by pressureInfluence).
    if (m_params.pressureInfluence > 0.0) {
        const qreal pressureFactor = 0.35 + 1.05 * qBound(0.0, raw.pressure(), 1.0);
        cutoff *= (1.0 - m_params.pressureInfluence) + m_params.pressureInfluence * pressureFactor;
    }

    cutoff = qMax(cutoff, minimumCutoff);

    // 4. Move the filtered position part of the way toward the pen.
    const qreal alpha = lowPassAlpha(cutoff, dtSeconds);
    const QPointF newPos = m_filtered.pos() + alpha * (raw.pos() - m_filtered.pos());

    // Pressure, tilt, rotation etc. follow the pen directly. Only the path is
    // smoothed, so pressure tapers stay where the artist put them.
    m_filtered = raw;
    m_filtered.setPos(newPos);
}

void KisCurveSmoother::addSample(const KisPaintInformation &raw, QVector<Segment> &out)
{
    if (!m_active || m_tailActive) return;

    qreal dt = (raw.currentTime() - m_lastTime) / 1000.0;
    if (dt <= 0.0) {
        dt = DEFAULT_DT;
    }
    dt = qBound(MIN_DT, dt, MAX_DT);

    const QPointF previousRawPos = m_lastRaw.pos();
    m_lastRaw = raw;
    m_lastTime = raw.currentTime();

    filterTowards(raw, previousRawPos, dt, 0.0);
    pushPoint(m_filtered, out, false);
}

bool KisCurveSmoother::catchUpStep(int timeMs, QVector<Segment> &out)
{
    if (!m_active || m_tailActive) return false;
    if (lagInScreenPixels() <= CATCH_UP_DONE_DISTANCE) return false;

    const qreal dt = qBound(MIN_DT, (timeMs - m_lastTime) / 1000.0, MAX_DT);
    m_lastTime = timeMs;

    // Pretend the pen sent its last sample again (it isn't moving).
    filterTowards(m_lastRaw, m_lastRaw.pos(), dt, CATCH_UP_MIN_CUTOFF);
    pushPoint(m_filtered, out, false);

    return lagInScreenPixels() > CATCH_UP_DONE_DISTANCE;
}

void KisCurveSmoother::end(bool catchUp, QVector<Segment> &out)
{
    if (!m_active) return;

    // A running tail animation always lands on the pen.
    if (m_tailActive) {
        catchUp = true;
        m_tailActive = false;
    }

    if (catchUp) {
        // Ease into the pen-up point right away: keep running the filter with
        // the pen "held" at its last position. Each step closes a fraction of
        // the gap, so the points get closer together as the line arrives.
        for (int i = 0; i < MAX_CATCH_UP_STEPS && lagInScreenPixels() > CATCH_UP_DONE_DISTANCE; i++) {
            m_lastTime += CATCH_UP_DT * 1000.0;
            filterTowards(m_lastRaw, m_lastRaw.pos(), CATCH_UP_DT, CATCH_UP_MIN_CUTOFF);
            pushPoint(m_filtered, out, false);
        }
        // Land exactly on the pen-up point.
        m_filtered = m_lastRaw;
        pushPoint(m_lastRaw, out, true);
    }

    closeCurve(out);
    m_active = false;
}

void KisCurveSmoother::startTail(int nowMs, int durationMs)
{
    if (!m_active) return;
    m_tailActive = true;
    m_tailStart = m_filtered.pos();
    m_tailStartTime = nowMs;
    m_tailDuration = qMax(0, durationMs);
}

bool KisCurveSmoother::tailStep(int nowMs, QVector<Segment> &out)
{
    if (!m_active) return true;
    if (!m_tailActive) return false;

    const qreal u = m_tailDuration > 0
        ? qBound(0.0, qreal(nowMs - m_tailStartTime) / m_tailDuration, 1.0)
        : 1.0;

    if (u >= 1.0) {
        end(true, out); // lands exactly on the pen and closes the curve
        return true;
    }

    // Ease-out cubic: covers most of the distance early, then settles in
    // gently. This is what makes the line look like it glides into place.
    const qreal eased = 1.0 - std::pow(1.0 - u, 3.0);
    const QPointF target = m_lastRaw.pos();

    m_filtered = m_lastRaw;
    m_filtered.setPos(m_tailStart + (target - m_tailStart) * eased);
    pushPoint(m_filtered, out, false);
    return false;
}

void KisCurveSmoother::closeCurve(QVector<Segment> &out)
{
    // A phantom copy of the last point gives the final segment its end
    // direction and flushes it out of the window.
    if (m_window.size() >= 2) {
        m_window.append(m_window.last());
        if (m_window.size() == 4) {
            emitSegment(out);
        }
    }
    m_window.clear();
}

KisPaintInformation KisCurveSmoother::lastSmoothedInfo() const
{
    return m_window.isEmpty() ? m_filtered : m_window.last();
}

QPolygonF KisCurveSmoother::pendingPath() const
{
    QPolygonF path;
    if (!m_active) return path;

    // window[1] is where the last painted segment ended; the rest of the
    // window, the filter head and the pen are not painted yet.
    for (int i = qMin(1, m_window.size() - 1); i >= 0 && i < m_window.size(); i++) {
        if (path.isEmpty() || path.last() != m_window[i].pos()) {
            path << m_window[i].pos();
        }
    }
    if (path.isEmpty() || path.last() != m_filtered.pos()) {
        path << m_filtered.pos();
    }
    if (path.last() != m_lastRaw.pos()) {
        path << m_lastRaw.pos();
    }
    return path;
}

qreal KisCurveSmoother::lagInScreenPixels() const
{
    return length(m_lastRaw.pos() - m_filtered.pos()) * m_params.zoom;
}

void KisCurveSmoother::pushPoint(const KisPaintInformation &info, QVector<Segment> &out, bool force)
{
    const qreal spacing = length(info.pos() - m_window.last().pos()) * m_params.zoom;
    if (spacing < MIN_POINT_SPACING) {
        if (!force || spacing < EPSILON) {
            return;
        }
        // Forced (final) point very close to the previous one: replace it
        // instead of adding a tiny segment.
        m_window.last() = info;
        return;
    }

    m_window.append(info);
    if (m_window.size() == 4) {
        emitSegment(out);
        m_window.removeFirst();
    }
}

void KisCurveSmoother::emitSegment(QVector<Segment> &out) const
{
    // Centripetal Catmull-Rom (alpha = 0.5) through P0..P3, converted to the
    // cubic Bézier between P1 and P2. d_i is the square root of each chord
    // length; using it as the spacing between knots is what prevents
    // overshoot and loops at sharp turns.
    const QPointF p0 = m_window[0].pos();
    const QPointF p1 = m_window[1].pos();
    const QPointF p2 = m_window[2].pos();
    const QPointF p3 = m_window[3].pos();

    const qreal d1 = std::sqrt(length(p1 - p0));
    const qreal d2 = std::sqrt(length(p2 - p1));
    const qreal d3 = std::sqrt(length(p3 - p2));

    if (d2 < EPSILON) {
        return; // zero-length segment, nothing to paint
    }

    // Default: a straight third of the way along the segment. Used for the
    // phantom start/end points and at corners.
    QPointF control1 = p1 + (p2 - p1) / 3.0;
    QPointF control2 = p2 + (p1 - p2) / 3.0;

    if (d1 > EPSILON && !isCorner(p0, p1, p2)) {
        control1 = (d1 * d1 * p2 - d2 * d2 * p0 + (2 * d1 * d1 + 3 * d1 * d2 + d2 * d2) * p1)
                   / (3 * d1 * (d1 + d2));
    }
    if (d3 > EPSILON && !isCorner(p1, p2, p3)) {
        control2 = (d3 * d3 * p1 - d2 * d2 * p3 + (2 * d3 * d3 + 3 * d3 * d2 + d2 * d2) * p2)
                   / (3 * d3 * (d3 + d2));
    }

    out.append(Segment{m_window[1], control1, control2, m_window[2]});
}
