/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GigaStrokePath.h"

#include <QMap>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

#include <klocalizedstring.h>
#include <kis_image.h>
#include <kis_node.h>
#include <kis_selection.h>

namespace {

/// Longest straight piece a painting call is cut into (image px).
const qreal MaxPieceLength = 2.0;

QPointF bezierPoint(const QPointF &p0, const QPointF &c1, const QPointF &c2, const QPointF &p1, qreal t)
{
    const qreal u = 1.0 - t;
    return u * u * u * p0 + 3 * u * u * t * c1 + 3 * u * t * t * c2 + t * t * t * p1;
}

/// Position of a painting call at parameter t (0 = start, 1 = end).
QPointF jobPoint(const GigaLastStrokeRecord::Job &job, qreal t)
{
    switch (job.type) {
    case GigaLastStrokeRecord::Line:
        return job.pi1.pos() + t * (job.pi2.pos() - job.pi1.pos());
    case GigaLastStrokeRecord::Curve:
        return bezierPoint(job.pi1.pos(), job.control1, job.control2, job.pi2.pos(), t);
    case GigaLastStrokeRecord::Point:
    default:
        return job.pi1.pos();
    }
}

/// How many pieces a call is cut into, from its length on the main copy.
int pieceCount(const GigaLastStrokeRecord::Job &job)
{
    if (job.type == GigaLastStrokeRecord::Point) return 0;
    qreal length = 0.0;
    QPointF prev = jobPoint(job, 0.0);
    const int probes = job.type == GigaLastStrokeRecord::Curve ? 16 : 1;
    for (int i = 1; i <= probes; ++i) {
        const QPointF p = jobPoint(job, qreal(i) / probes);
        length += std::hypot(p.x() - prev.x(), p.y() - prev.y());
        prev = p;
    }
    return qMax(1, int(std::ceil(length / MaxPieceLength)));
}

/// The paint information of a call at parameter t: pressure, tilt, time...
/// blended between its two ends, at the call's own position for t.
KisPaintInformation jobInfo(const GigaLastStrokeRecord::Job &job, qreal t)
{
    if (t <= 0.0) return job.pi1;
    if (t >= 1.0) return job.pi2;
    return KisPaintInformation::mix(jobPoint(job, t), t, job.pi1, job.pi2);
}

/**
 * Monotone cubic through the vertices' offsets, one curve for x and one for
 * y (Fritsch-Butland tangents). "Monotone": between two dots the offset
 * never goes beyond the dots' own offsets, so the line bends only where
 * you pulled it and never bulges the other way.
 */
struct OffsetSpline
{
    QVector<qreal> s;
    QVector<QPointF> y;
    QVector<QPointF> m;

    explicit OffsetSpline(const QVector<GigaStrokePath::Vertex> &vertices)
    {
        for (const GigaStrokePath::Vertex &v : vertices) {
            s << v.s;
            y << v.offset;
        }
        const int n = s.size();
        m.resize(n);
        if (n < 2) return;

        auto tangents = [&](auto component, auto setComponent) {
            QVector<qreal> delta(n - 1);
            for (int k = 0; k < n - 1; ++k) {
                const qreal h = s[k + 1] - s[k];
                delta[k] = h > 0 ? (component(y[k + 1]) - component(y[k])) / h : 0.0;
            }
            setComponent(m[0], delta[0]);
            setComponent(m[n - 1], delta[n - 2]);
            for (int k = 1; k < n - 1; ++k) {
                const qreal d0 = delta[k - 1];
                const qreal d1 = delta[k];
                if (d0 * d1 <= 0.0) {
                    setComponent(m[k], 0.0);
                } else {
                    const qreal h0 = s[k] - s[k - 1];
                    const qreal h1 = s[k + 1] - s[k];
                    const qreal w1 = 2 * h1 + h0;
                    const qreal w2 = h1 + 2 * h0;
                    setComponent(m[k], (w1 + w2) / (w1 / d0 + w2 / d1));
                }
            }
        };
        tangents([](const QPointF &p) { return p.x(); }, [](QPointF &p, qreal v) { p.setX(v); });
        tangents([](const QPointF &p) { return p.y(); }, [](QPointF &p, qreal v) { p.setY(v); });
    }

    QPointF at(qreal t) const
    {
        const int n = s.size();
        if (n == 0) return QPointF();
        if (n == 1 || t <= s[0]) return y[0];
        if (t >= s[n - 1]) return y[n - 1];
        const int k = int(std::upper_bound(s.begin(), s.end(), t) - s.begin()) - 1;
        const qreal h = s[k + 1] - s[k];
        if (h <= 0) return y[k];
        const qreal u = (t - s[k]) / h;
        const qreal u2 = u * u;
        const qreal u3 = u2 * u;
        const qreal h00 = 2 * u3 - 3 * u2 + 1;
        const qreal h10 = u3 - 2 * u2 + u;
        const qreal h01 = -2 * u3 + 3 * u2;
        const qreal h11 = u3 - u2;
        return h00 * y[k] + h10 * h * m[k] + h01 * y[k + 1] + h11 * h * m[k + 1];
    }
};

/// The whole bend at distance s: the dots' curve plus every proportional move.
struct OffsetEvaluator
{
    OffsetSpline spline;
    const QVector<GigaStrokePath::Move> &moves;

    OffsetEvaluator(const QVector<GigaStrokePath::Vertex> &vertices, const QVector<GigaStrokePath::Move> &m)
        : spline(vertices), moves(m) {}

    QPointF at(qreal s) const
    {
        QPointF offset = spline.at(s);
        for (const GigaStrokePath::Move &move : moves) {
            const qreal w = GigaStrokePath::moveWeight(move, s);
            if (w > 0.0) offset += w * move.delta;
        }
        return offset;
    }
};

} // namespace

qreal GigaStrokePath::falloffWeight(int falloff, qreal t)
{
    if (t >= 1.0) return 0.0;
    if (t <= 0.0) return 1.0;
    const qreal x = 1.0 - t; // 1 at the centre, 0 at the radius
    switch (falloff) {
    case Sphere:   return std::sqrt(qMax<qreal>(0.0, 2.0 * x - x * x));
    case Root:     return std::sqrt(x);
    case Sharp:    return x * x;
    case Linear:   return x;
    case Constant: return 1.0;
    case Smooth:
    default:       return x * x * (3.0 - 2.0 * x);
    }
}

qreal GigaStrokePath::moveWeight(const Move &move, qreal s)
{
    if (move.radius <= 0.0 || move.centers.isEmpty()) return 0.0;
    qreal nearest = std::numeric_limits<qreal>::max();
    for (qreal c : move.centers) nearest = qMin(nearest, qAbs(s - c));
    return falloffWeight(move.falloff, nearest / move.radius);
}

bool GigaStrokePath::build(const GigaLastStrokeRecord &record, QString *error)
{
    m_pieces.clear();
    m_linear.clear();
    m_points.clear();
    m_lengths.clear();
    m_vertices.clear();
    m_moves.clear();

    auto fail = [&](const QString &message) {
        m_pieces.clear();
        m_points.clear();
        m_lengths.clear();
        if (error) *error = message;
        return false;
    };

    // Split the calls per copy (copy 0 = the stroke you drew).
    QMap<int, QVector<int>> perCopy;
    for (int i = 0; i < record.jobs.size(); ++i) {
        perCopy[record.jobs[i].strokeInfoId] << i;
    }
    if (!perCopy.contains(0) || perCopy[0].isEmpty()) {
        return fail(i18n("Edit Stroke Points: the stroke has no points."));
    }
    const QVector<int> &mainIdx = perCopy[0];
    const int copies = perCopy.lastKey() + 1;
    for (auto it = perCopy.constBegin(); it != perCopy.constEnd(); ++it) {
        if (it.key() < 0 || it.value().size() != mainIdx.size()) {
            return fail(i18n("Edit Stroke Points doesn't work with this multi-brush stroke."));
        }
        for (int i = 0; i < mainIdx.size(); ++i) {
            if (record.jobs[it.value()[i]].type != record.jobs[mainIdx[i]].type) {
                return fail(i18n("Edit Stroke Points doesn't work with this multi-brush stroke."));
            }
        }
    }

    // Distances along the main copy, piece by piece.
    QVector<GigaLastStrokeRecord::Job> mainJobs;
    QVector<qreal> mainS1, mainS2;
    qreal s = 0.0;
    QPointF last = record.jobs[mainIdx[0]].pi1.pos();
    m_points << last;
    m_lengths << 0.0;
    for (int i : mainIdx) {
        const GigaLastStrokeRecord::Job &job = record.jobs[i];
        mainJobs << job;
        const QPointF start = job.pi1.pos();
        // A jump between calls (rare) still counts as distance.
        const qreal jump = std::hypot(start.x() - last.x(), start.y() - last.y());
        if (jump > 1e-6) {
            s += jump;
            m_points << start;
            m_lengths << s;
        }
        mainS1 << s;
        const int n = pieceCount(job);
        QPointF prev = start;
        for (int k = 1; k <= n; ++k) {
            const QPointF p = jobPoint(job, qreal(k) / n);
            s += std::hypot(p.x() - prev.x(), p.y() - prev.y());
            m_points << p;
            m_lengths << s;
            prev = p;
        }
        mainS2 << s;
        last = prev;
    }

    // Each extra copy as an affine map of the main copy (least squares).
    m_linear.resize(copies);
    m_linear[0] = QTransform();
    for (int c = 1; c < copies; ++c) {
        if (!perCopy.contains(c)) continue;
        const QVector<int> &idx = perCopy[c];
        QVector<QPointF> a, b; // main, copy
        for (int i = 0; i < idx.size(); ++i) {
            a << record.jobs[mainIdx[i]].pi1.pos();
            b << record.jobs[idx[i]].pi1.pos();
            if (record.jobs[mainIdx[i]].type != GigaLastStrokeRecord::Point) {
                a << record.jobs[mainIdx[i]].pi2.pos();
                b << record.jobs[idx[i]].pi2.pos();
            }
        }
        QPointF ca, cb;
        for (int i = 0; i < a.size(); ++i) { ca += a[i]; cb += b[i]; }
        ca /= a.size();
        cb /= b.size();
        qreal sxx = 0, sxy = 0, syy = 0, bx_ax = 0, bx_ay = 0, by_ax = 0, by_ay = 0;
        for (int i = 0; i < a.size(); ++i) {
            const QPointF p = a[i] - ca;
            const QPointF q = b[i] - cb;
            sxx += p.x() * p.x(); sxy += p.x() * p.y(); syy += p.y() * p.y();
            bx_ax += q.x() * p.x(); bx_ay += q.x() * p.y();
            by_ax += q.y() * p.x(); by_ay += q.y() * p.y();
        }
        const qreal det = sxx * syy - sxy * sxy;
        if (det <= 1e-6 * qMax(1.0, (sxx + syy) * (sxx + syy))) {
            return fail(i18n("Edit Stroke Points: a straight multi-brush line can't be edited (try Pixels mode)."));
        }
        // A = (B^T A)(A^T A)^-1, written out for 2x2.
        const qreal i11 = syy / det, i12 = -sxy / det, i22 = sxx / det;
        const qreal m11 = bx_ax * i11 + bx_ay * i12;
        const qreal m12 = bx_ax * i12 + bx_ay * i22;
        const qreal m21 = by_ax * i11 + by_ay * i12;
        const qreal m22 = by_ax * i12 + by_ay * i22;
        qreal worst = 0.0;
        for (int i = 0; i < a.size(); ++i) {
            const QPointF p = a[i] - ca;
            const QPointF fit(m11 * p.x() + m12 * p.y() + cb.x(), m21 * p.x() + m22 * p.y() + cb.y());
            worst = qMax(worst, std::hypot(fit.x() - b[i].x(), fit.y() - b[i].y()));
        }
        if (worst > 2.0) {
            return fail(i18n("Edit Stroke Points doesn't work with this multi-brush stroke (try Pixels mode)."));
        }
        // QTransform maps (x, y) to (m11 x + m21 y, m12 x + m22 y).
        m_linear[c] = QTransform(m11, m21, m12, m22, 0, 0);
    }

    // The pieces of every copy, in recording order (keeps the dab order).
    QVector<int> mainOf(record.jobs.size(), -1);
    for (auto it = perCopy.constBegin(); it != perCopy.constEnd(); ++it) {
        for (int i = 0; i < it.value().size(); ++i) mainOf[it.value()[i]] = i;
    }
    for (int i = 0; i < record.jobs.size(); ++i) {
        addPieces(record.jobs[i], mainOf[i], mainJobs, mainS1, mainS2);
    }

    m_vertices << Vertex{0.0, QPointF()};
    return true;
}

void GigaStrokePath::addPieces(const GigaLastStrokeRecord::Job &job, int mainIndex,
                               const QVector<GigaLastStrokeRecord::Job> &mainJobs,
                               const QVector<qreal> &mainS1, const QVector<qreal> &mainS2)
{
    if (mainIndex < 0 || mainIndex >= mainJobs.size()) return;
    const qreal s1 = mainS1[mainIndex];
    const qreal s2 = mainS2[mainIndex];
    if (job.type == GigaLastStrokeRecord::Point) {
        Piece piece;
        piece.job = job;
        piece.s1 = piece.s2 = s1;
        m_pieces << piece;
        return;
    }
    // Same cut points as the main copy, so every piece has a matching s.
    const GigaLastStrokeRecord::Job &main = mainJobs[mainIndex];
    const int n = pieceCount(main);
    // Distances of the main copy's cut points (curves aren't evenly spaced).
    QVector<qreal> cutS(n + 1);
    cutS[0] = s1;
    QPointF prev = jobPoint(main, 0.0);
    for (int k = 1; k <= n; ++k) {
        const QPointF p = jobPoint(main, qreal(k) / n);
        cutS[k] = cutS[k - 1] + std::hypot(p.x() - prev.x(), p.y() - prev.y());
        prev = p;
    }
    cutS[n] = s2;
    KisPaintInformation from = jobInfo(job, 0.0);
    for (int k = 1; k <= n; ++k) {
        const KisPaintInformation to = jobInfo(job, qreal(k) / n);
        Piece piece;
        piece.job.type = GigaLastStrokeRecord::Line;
        piece.job.strokeInfoId = job.strokeInfoId;
        piece.job.pi1 = from;
        piece.job.pi2 = to;
        piece.s1 = cutS[k - 1];
        piece.s2 = cutS[k];
        m_pieces << piece;
        from = to;
    }
}

QPointF GigaStrokePath::basePointAt(qreal s) const
{
    if (m_points.isEmpty()) return QPointF();
    if (s <= 0.0) return m_points.first();
    if (s >= m_lengths.last()) return m_points.last();
    const int k = int(std::upper_bound(m_lengths.begin(), m_lengths.end(), s) - m_lengths.begin()) - 1;
    const qreal h = m_lengths[k + 1] - m_lengths[k];
    if (h <= 0) return m_points[k];
    const qreal t = (s - m_lengths[k]) / h;
    return m_points[k] + t * (m_points[k + 1] - m_points[k]);
}

void GigaStrokePath::placeVertices(qreal spacing)
{
    if (!isValid()) return;
    spacing = qMax<qreal>(spacing, 2.0);
    const QVector<Move> oldMoves = m_moves;
    const OffsetEvaluator old(m_vertices, oldMoves);
    const qreal total = length();

    QVector<qreal> places;
    places << 0.0;
    if (total > 0.5) {
        // Direction of the line around distance s, over a short window so
        // the tremble of the hand doesn't count as turning.
        const qreal window = qMax<qreal>(2.0, spacing * 0.15);
        auto direction = [&](qreal at) {
            const QPointF d = basePointAt(qMin(total, at + window)) - basePointAt(qMax<qreal>(0.0, at - window));
            return std::atan2(d.y(), d.x());
        };
        // A dot every ~1.3 spacings on straight parts; turning 30 degrees
        // counts as much as a full gap, so curves get more dots.
        const qreal straightGap = spacing * 1.33;
        const qreal turnPerDot = M_PI / 6.0;
        const qreal minGap = spacing * 0.35;
        const qreal step = qMax<qreal>(1.0, spacing / 16.0);
        qreal cost = 0.0;
        qreal lastPlaced = 0.0;
        qreal prevAngle = direction(0.0);
        for (qreal at = step; at < total; at += step) {
            const qreal angle = direction(at);
            qreal turn = angle - prevAngle;
            while (turn > M_PI) turn -= 2 * M_PI;
            while (turn < -M_PI) turn += 2 * M_PI;
            prevAngle = angle;
            cost += step / straightGap + qAbs(turn) / turnPerDot;
            if (cost >= 1.0 && at - lastPlaced >= minGap && total - at >= minGap) {
                places << at;
                lastPlaced = at;
                cost = 0.0;
            }
        }
        places << total;
    }

    QVector<Vertex> fresh;
    for (qreal at : places) {
        fresh << Vertex{at, old.at(at)};
    }
    m_vertices = fresh;
    m_moves.clear(); // folded into the new dots
}

void GigaStrokePath::setOffset(int index, const QPointF &offset)
{
    if (index >= 0 && index < m_vertices.size()) m_vertices[index].offset = offset;
}

int GigaStrokePath::insertVertex(qreal s)
{
    if (!isValid()) return -1;
    s = qBound<qreal>(0.0, s, length());
    for (int i = 0; i < m_vertices.size(); ++i) {
        if (qAbs(m_vertices[i].s - s) < 0.5) return i;
    }
    // Only the dots' own curve: the proportional moves are added on top.
    const QPointF offset = OffsetSpline(m_vertices).at(s);
    int at = 0;
    while (at < m_vertices.size() && m_vertices[at].s < s) ++at;
    m_vertices.insert(at, Vertex{s, offset});
    return at;
}

QPointF GigaStrokePath::vertexPosition(int index) const
{
    if (index < 0 || index >= m_vertices.size()) return QPointF();
    return basePointAt(m_vertices[index].s) + offsetAt(m_vertices[index].s);
}

QPointF GigaStrokePath::offsetAt(qreal s) const
{
    return OffsetEvaluator(m_vertices, m_moves).at(s);
}

QVector<QPointF> GigaStrokePath::bentPolyline() const
{
    const OffsetEvaluator spline(m_vertices, m_moves);
    QVector<QPointF> result;
    result.reserve(m_points.size());
    for (int i = 0; i < m_points.size(); ++i) {
        result << m_points[i] + spline.at(m_lengths[i]);
    }
    return result;
}

QVector<QPointF> GigaStrokePath::bentPolyline(const Shape &shape) const
{
    const OffsetEvaluator spline(shape.vertices, shape.moves);
    QVector<QPointF> result;
    result.reserve(m_points.size());
    for (int i = 0; i < m_points.size(); ++i) {
        result << m_points[i] + spline.at(m_lengths[i]);
    }
    return result;
}

QPointF GigaStrokePath::bentPointAt(qreal s) const
{
    return basePointAt(s) + offsetAt(s);
}

QRectF GigaStrokePath::bentBounds() const
{
    const QVector<QPointF> line = bentPolyline();
    if (line.isEmpty()) return QRectF();
    qreal x0 = line[0].x(), x1 = x0, y0 = line[0].y(), y1 = y0;
    for (const QPointF &p : line) {
        x0 = qMin(x0, p.x()); x1 = qMax(x1, p.x());
        y0 = qMin(y0, p.y()); y1 = qMax(y1, p.y());
    }
    return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}

qreal GigaStrokePath::nearestS(const QPointF &p, qreal *distance) const
{
    const QVector<QPointF> line = bentPolyline();
    qreal bestS = 0.0;
    qreal best = std::numeric_limits<qreal>::max();
    for (int i = 0; i < line.size(); ++i) {
        qreal t = 0.0;
        QPointF q = line[i];
        if (i + 1 < line.size()) {
            const QPointF d = line[i + 1] - line[i];
            const qreal len2 = d.x() * d.x() + d.y() * d.y();
            if (len2 > 0) {
                t = qBound<qreal>(0.0, QPointF::dotProduct(p - line[i], d) / len2, 1.0);
                q = line[i] + t * d;
            }
        }
        const qreal dist = std::hypot(p.x() - q.x(), p.y() - q.y());
        if (dist < best) {
            best = dist;
            bestS = i + 1 < line.size() ? m_lengths[i] + t * (m_lengths[i + 1] - m_lengths[i]) : m_lengths[i];
        }
    }
    if (distance) *distance = best;
    return bestS;
}

bool GigaStrokePath::isEdited() const
{
    for (const Vertex &v : m_vertices) {
        if (qAbs(v.offset.x()) > 1e-3 || qAbs(v.offset.y()) > 1e-3) return true;
    }
    for (const Move &m : m_moves) {
        if (qAbs(m.delta.x()) > 1e-3 || qAbs(m.delta.y()) > 1e-3) return true;
    }
    return false;
}

QVector<GigaLastStrokeRecord::Job> GigaStrokePath::bentJobs() const
{
    const OffsetEvaluator spline(m_vertices, m_moves);
    QVector<GigaLastStrokeRecord::Job> jobs;
    jobs.reserve(m_pieces.size());
    for (const Piece &piece : m_pieces) {
        GigaLastStrokeRecord::Job job = piece.job;
        const int copy = job.strokeInfoId;
        const QTransform &map = (copy >= 0 && copy < m_linear.size()) ? m_linear[copy] : QTransform();
        job.pi1.setPos(job.pi1.pos() + map.map(spline.at(piece.s1)));
        if (job.type != GigaLastStrokeRecord::Point) {
            job.pi2.setPos(job.pi2.pos() + map.map(spline.at(piece.s2)));
        }
        jobs << job;
    }
    return jobs;
}
