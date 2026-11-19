/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisQuickShapeRecognizer.h"

#include <QLineF>
#include <QtMath>

#include <klocalizedstring.h>

#include <algorithm>
#include <numeric>

namespace {

// Thresholds are relative to the stroke's size, so they work at any scale.
constexpr qreal LINE_MAX_DEVIATION = 0.045;   // of chord length
constexpr qreal CLOSED_GAP = 0.2;             // end-to-start gap, of bbox size
constexpr qreal SIMPLIFY_EPSILON = 0.06;      // corner detection, of bbox diagonal
constexpr qreal POLYGON_MAX_ERROR = 0.07;     // of bbox diagonal
constexpr qreal SHARP_TURN_DEGREES = 50.0;    // a "corner" turns at least this much
constexpr qreal ELLIPSE_MAX_ERROR = 0.09;     // RMS of normalized radius error
constexpr qreal ARC_MAX_ERROR = 0.05;         // RMS radial error, of radius
constexpr int RESAMPLE_COUNT = 96;

qreal length(const QPointF &p) { return std::sqrt(p.x() * p.x() + p.y() * p.y()); }

// Evenly spaced points along the stroke, so dense slow parts don't dominate.
QVector<QPointF> resample(const QVector<QPointF> &pts, int count)
{
    qreal total = 0;
    for (int i = 1; i < pts.size(); i++) total += length(pts[i] - pts[i - 1]);
    QVector<QPointF> out;
    if (total <= 0 || pts.size() < 2) return pts;
    const qreal step = total / (count - 1);
    out << pts.first();
    qreal carried = 0;
    for (int i = 1; i < pts.size() && out.size() < count; i++) {
        QPointF a = pts[i - 1];
        const QPointF b = pts[i];
        qreal seg = length(b - a);
        while (carried + seg >= step && out.size() < count) {
            const qreal t = (step - carried) / seg;
            a = a + (b - a) * t;
            out << a;
            seg = length(b - a);
            carried = 0;
        }
        carried += seg;
    }
    while (out.size() < count) out << pts.last();
    return out;
}

qreal distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 <= 0) return length(p - a);
    const qreal t = qBound(0.0, ((p - a).x() * ab.x() + (p - a).y() * ab.y()) / len2, 1.0);
    return length(p - (a + ab * t));
}

// Ramer-Douglas-Peucker: keeps the points that define the stroke's corners.
void simplify(const QVector<QPointF> &pts, int first, int last, qreal epsilon, QVector<int> &keep)
{
    qreal maxDist = 0;
    int index = -1;
    for (int i = first + 1; i < last; i++) {
        const qreal d = distanceToSegment(pts[i], pts[first], pts[last]);
        if (d > maxDist) {
            maxDist = d;
            index = i;
        }
    }
    if (index >= 0 && maxDist > epsilon) {
        simplify(pts, first, index, epsilon, keep);
        keep << index;
        simplify(pts, index, last, epsilon, keep);
    }
}

QVector<QPointF> corners(const QVector<QPointF> &pts, qreal epsilon)
{
    QVector<int> keep;
    keep << 0;
    simplify(pts, 0, pts.size() - 1, epsilon, keep);
    keep << pts.size() - 1;
    std::sort(keep.begin(), keep.end());
    QVector<QPointF> out;
    for (int i : keep) out << pts[i];
    return out;
}

// Direction change at b on the way a -> b -> c, in degrees (0 = straight on).
qreal turnDegrees(const QPointF &a, const QPointF &b, const QPointF &c)
{
    const QPointF u = b - a, v = c - b;
    const qreal lu = length(u), lv = length(v);
    if (lu <= 0 || lv <= 0) return 0;
    const qreal cosine = qBound(-1.0, (u.x() * v.x() + u.y() * v.y()) / (lu * lv), 1.0);
    return qRadiansToDegrees(std::acos(cosine));
}

qreal maxDistanceToPolyline(const QVector<QPointF> &pts, const QVector<QPointF> &poly, bool closed)
{
    qreal worst = 0;
    for (const QPointF &p : pts) {
        qreal best = std::numeric_limits<qreal>::max();
        const int n = poly.size();
        for (int i = 0; i + 1 < n; i++) best = qMin(best, distanceToSegment(p, poly[i], poly[i + 1]));
        if (closed && n > 2) best = qMin(best, distanceToSegment(p, poly.last(), poly.first()));
        worst = qMax(worst, best);
    }
    return worst;
}

QPointF centroid(const QVector<QPointF> &pts)
{
    QPointF sum;
    for (const QPointF &p : pts) sum += p;
    return sum / qMax(1, int(pts.size()));
}

QPainterPath polygonPath(const QVector<QPointF> &poly, bool closed)
{
    QPainterPath path;
    if (poly.isEmpty()) return path;
    path.moveTo(poly.first());
    for (int i = 1; i < poly.size(); i++) path.lineTo(poly[i]);
    if (closed) path.lineTo(poly.first());
    return path;
}

// Rectangle aligned with its first edge, fitted around four corners.
QVector<QPointF> fitRectangle(const QVector<QPointF> &quad, bool square)
{
    const QPointF c = centroid(quad);
    const QPointF edge = quad[1] - quad[0];
    const qreal angle = std::atan2(edge.y(), edge.x());
    const qreal ca = std::cos(-angle), sa = std::sin(-angle);
    qreal minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
    for (const QPointF &p : quad) {
        const QPointF d = p - c;
        const qreal x = d.x() * ca - d.y() * sa;
        const qreal y = d.x() * sa + d.y() * ca;
        minX = qMin(minX, x); maxX = qMax(maxX, x);
        minY = qMin(minY, y); maxY = qMax(maxY, y);
    }
    qreal hw = (maxX - minX) / 2, hh = (maxY - minY) / 2;
    if (square) hw = hh = (hw + hh) / 2;
    const qreal cb = std::cos(angle), sb = std::sin(angle);
    auto back = [&](qreal x, qreal y) { return c + QPointF(x * cb - y * sb, x * sb + y * cb); };
    return {back(-hw, -hh), back(hw, -hh), back(hw, hh), back(-hw, hh)};
}

} // namespace

QString KisQuickShapeRecognizer::typeName(Type type)
{
    switch (type) {
    case Line: return i18nc("QuickShape", "Line");
    case Polyline: return i18nc("QuickShape", "Polyline");
    case Arc: return i18nc("QuickShape", "Arc");
    case Ellipse: return i18nc("QuickShape", "Ellipse");
    case Rectangle: return i18nc("QuickShape", "Rectangle");
    case Polygon: return i18nc("QuickShape", "Polygon");
    default: return QString();
    }
}

KisQuickShapeRecognizer::Result KisQuickShapeRecognizer::recognize(const QVector<QPointF> &input, bool perfect)
{
    Result result;
    if (input.size() < 5) return result;

    const QVector<QPointF> pts = resample(input, RESAMPLE_COUNT);
    QRectF bbox(pts.first(), QSizeF());
    for (const QPointF &p : pts) bbox |= QRectF(p, QSizeF(0.001, 0.001));
    const qreal diag = length(QPointF(bbox.width(), bbox.height()));
    if (diag < 4.0) return result; // a dot, not a shape

    const QPointF start = pts.first(), end = pts.last();
    const qreal chord = length(end - start);

    // 1. Line.
    if (chord > 0.5 * diag) {
        qreal deviation = 0;
        for (const QPointF &p : pts) deviation = qMax(deviation, distanceToSegment(p, start, end));
        if (deviation < LINE_MAX_DEVIATION * chord) {
            QPointF b = end;
            if (perfect) {
                const qreal step = qDegreesToRadians(15.0);
                const qreal angle = std::round(std::atan2(end.y() - start.y(), end.x() - start.x()) / step) * step;
                b = start + QPointF(std::cos(angle), std::sin(angle)) * chord;
            }
            result.type = Line;
            result.path.moveTo(start);
            result.path.lineTo(b);
            return result;
        }
    }

    const qreal maxSide = qMax(bbox.width(), bbox.height());
    const bool closed = chord < CLOSED_GAP * maxSide;

    if (closed) {
        // 2a. Polygon / rectangle: a few sharp corners.
        // Start from the point farthest from the center (a likely corner),
        // so the seam where the stroke closes doesn't hide one.
        const QPointF c = centroid(pts);
        int far = 0;
        for (int i = 1; i < pts.size(); i++) if (length(pts[i] - c) > length(pts[far] - c)) far = i;
        QVector<QPointF> loop;
        for (int i = 0; i < pts.size(); i++) loop << pts[(far + i) % pts.size()];
        loop << loop.first();

        QVector<QPointF> poly = corners(loop, SIMPLIFY_EPSILON * diag);
        poly.removeLast(); // same as the first point
        if (poly.size() >= 3 && poly.size() <= 6) {
            qreal minTurn = 180;
            for (int i = 0; i < poly.size(); i++) {
                minTurn = qMin(minTurn, turnDegrees(poly[(i + poly.size() - 1) % poly.size()], poly[i], poly[(i + 1) % poly.size()]));
            }
            const qreal error = maxDistanceToPolyline(pts, poly, true) / diag;
            if (minTurn > SHARP_TURN_DEGREES && error < POLYGON_MAX_ERROR) {
                bool rightAngles = poly.size() == 4;
                for (int i = 0; rightAngles && i < 4; i++) {
                    rightAngles = qAbs(turnDegrees(poly[(i + 3) % 4], poly[i], poly[(i + 1) % 4]) - 90.0) < 22.0;
                }
                if (rightAngles) {
                    result.type = Rectangle;
                    result.path = polygonPath(fitRectangle(poly, perfect), true);
                } else {
                    result.type = Polygon;
                    result.path = polygonPath(poly, true);
                }
                return result;
            }
        }

        // 2b. Ellipse, from the spread of the points (principal axes).
        qreal sxx = 0, syy = 0, sxy = 0;
        for (const QPointF &p : pts) {
            const QPointF d = p - c;
            sxx += d.x() * d.x(); syy += d.y() * d.y(); sxy += d.x() * d.y();
        }
        sxx /= pts.size(); syy /= pts.size(); sxy /= pts.size();
        const qreal angle = 0.5 * std::atan2(2 * sxy, sxx - syy);
        const qreal ca = std::cos(angle), sa = std::sin(angle);
        qreal vu = 0, vv = 0;
        for (const QPointF &p : pts) {
            const QPointF d = p - c;
            const qreal u = d.x() * ca + d.y() * sa;
            const qreal v = -d.x() * sa + d.y() * ca;
            vu += u * u; vv += v * v;
        }
        // Points spread evenly around an ellipse have variance r^2 / 2 per axis.
        qreal ru = std::sqrt(2 * vu / pts.size()), rv = std::sqrt(2 * vv / pts.size());
        if (ru > 0 && rv > 0) {
            qreal err = 0;
            for (const QPointF &p : pts) {
                const QPointF d = p - c;
                const qreal u = (d.x() * ca + d.y() * sa) / ru;
                const qreal v = (-d.x() * sa + d.y() * ca) / rv;
                const qreal r = std::sqrt(u * u + v * v) - 1.0;
                err += r * r;
            }
            err = std::sqrt(err / pts.size());
            if (err < ELLIPSE_MAX_ERROR) {
                if (perfect) ru = rv = (ru + rv) / 2;
                QPainterPath ellipse;
                ellipse.addEllipse(QPointF(), ru, rv);
                QTransform t;
                t.translate(c.x(), c.y());
                t.rotateRadians(angle);
                result.type = Ellipse;
                result.path = t.map(ellipse);
                return result;
            }
        }
        return result;
    }

    // 3a. Polyline: a few sharp corners.
    QVector<QPointF> poly = corners(pts, SIMPLIFY_EPSILON * diag);
    if (poly.size() >= 3 && poly.size() <= 6) {
        qreal minTurn = 180;
        for (int i = 1; i + 1 < poly.size(); i++) minTurn = qMin(minTurn, turnDegrees(poly[i - 1], poly[i], poly[i + 1]));
        const qreal error = maxDistanceToPolyline(pts, poly, false) / diag;
        if (minTurn > SHARP_TURN_DEGREES && error < POLYGON_MAX_ERROR) {
            result.type = Polyline;
            result.path = polygonPath(poly, false);
            return result;
        }
    }

    // 3b. Arc: algebraic circle fit (Kasa), then check the points sit on it.
    {
        const QPointF c0 = centroid(pts);
        qreal suu = 0, svv = 0, suv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
        for (const QPointF &p : pts) {
            const qreal u = p.x() - c0.x(), v = p.y() - c0.y();
            suu += u * u; svv += v * v; suv += u * v;
            suuu += u * u * u; svvv += v * v * v; suvv += u * v * v; svuu += v * u * u;
        }
        const qreal det = suu * svv - suv * suv;
        if (qAbs(det) > 1e-9) {
            const qreal bu = 0.5 * (suuu + suvv), bv = 0.5 * (svvv + svuu);
            const qreal uc = (bu * svv - bv * suv) / det;
            const qreal vc = (bv * suu - bu * suv) / det;
            const QPointF center = c0 + QPointF(uc, vc);
            qreal radius = 0;
            for (const QPointF &p : pts) radius += length(p - center);
            radius /= pts.size();
            qreal err = 0;
            for (const QPointF &p : pts) {
                const qreal e = length(p - center) - radius;
                err += e * e;
            }
            err = std::sqrt(err / pts.size()) / qMax(radius, 1e-6);

            // Sweep: sum of signed angle steps along the stroke.
            qreal sweep = 0;
            for (int i = 1; i < pts.size(); i++) {
                const QPointF a = pts[i - 1] - center, b = pts[i] - center;
                sweep += std::atan2(a.x() * b.y() - a.y() * b.x(), a.x() * b.x() + a.y() * b.y());
            }
            const qreal sweepDeg = qRadiansToDegrees(sweep);
            if (err < ARC_MAX_ERROR && qAbs(sweepDeg) > 20 && qAbs(sweepDeg) < 340 && radius < 4 * diag) {
                const QPointF s = start - center;
                const qreal startDeg = -qRadiansToDegrees(std::atan2(s.y(), s.x())); // Qt: counter-clockwise, y up
                QPainterPath arc;
                arc.arcMoveTo(QRectF(center - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius)), startDeg);
                arc.arcTo(QRectF(center - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius)), startDeg, -sweepDeg);
                result.type = Arc;
                result.path = arc;
                return result;
            }
        }
    }

    return result;
}
