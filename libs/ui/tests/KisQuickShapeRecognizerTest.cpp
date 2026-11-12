/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisQuickShapeRecognizerTest.h"

#include "tool/KisQuickShapeRecognizer.h"

#include <QtMath>

using R = KisQuickShapeRecognizer;

namespace {

// Deterministic "hand wobble" of up to `amount` pixels.
QPointF wobble(int i, qreal amount)
{
    return QPointF(std::sin(i * 1.7) * amount, std::cos(i * 2.3) * amount * 0.8);
}

QVector<QPointF> polyline(const QVector<QPointF> &corners, int perSegment, qreal noise)
{
    QVector<QPointF> out;
    int k = 0;
    for (int c = 0; c + 1 < corners.size(); c++) {
        for (int i = 0; i < perSegment; i++) {
            const qreal t = qreal(i) / perSegment;
            out << corners[c] + (corners[c + 1] - corners[c]) * t + wobble(k++, noise);
        }
    }
    out << corners.last();
    return out;
}

QVector<QPointF> ellipse(QPointF c, qreal rx, qreal ry, qreal rotDeg, qreal fromDeg, qreal toDeg, qreal noise)
{
    QVector<QPointF> out;
    const qreal rot = qDegreesToRadians(rotDeg);
    for (int i = 0; i <= 120; i++) {
        const qreal a = qDegreesToRadians(fromDeg + (toDeg - fromDeg) * i / 120.0);
        const QPointF p(rx * std::cos(a), ry * std::sin(a));
        out << c + QPointF(p.x() * std::cos(rot) - p.y() * std::sin(rot),
                           p.x() * std::sin(rot) + p.y() * std::cos(rot)) + wobble(i, noise);
    }
    return out;
}

qreal radiusSpread(const QPainterPath &path)
{
    const QPointF c = path.boundingRect().center();
    qreal mn = 1e9, mx = 0;
    for (int i = 0; i <= 64; i++) {
        const qreal r = QLineF(c, path.pointAtPercent(i / 64.0)).length();
        mn = qMin(mn, r);
        mx = qMax(mx, r);
    }
    return mx - mn;
}

} // namespace

void KisQuickShapeRecognizerTest::testWobblyLine()
{
    const R::Result r = R::recognize(polyline({{0, 0}, {300, 40}}, 60, 3.0));
    QCOMPARE(r.type, R::Line);
}

void KisQuickShapeRecognizerTest::testPerfectLineSnapsAngle()
{
    const R::Result r = R::recognize(polyline({{0, 0}, {300, 12}}, 60, 1.0), true); // ~2 degrees
    QCOMPARE(r.type, R::Line);
    const QPointF a = r.path.elementAt(0), b = r.path.elementAt(1);
    QVERIFY2(qAbs(a.y() - b.y()) < 0.01, "should snap to horizontal");
}

void KisQuickShapeRecognizerTest::testCircle()
{
    QCOMPARE(R::recognize(ellipse({200, 200}, 100, 100, 0, 0, 360, 3.0)).type, R::Ellipse);
}

void KisQuickShapeRecognizerTest::testRotatedEllipse()
{
    QCOMPARE(R::recognize(ellipse({200, 200}, 150, 70, 30, 10, 365, 3.0)).type, R::Ellipse);
}

void KisQuickShapeRecognizerTest::testPerfectEllipseIsCircle()
{
    const R::Result r = R::recognize(ellipse({200, 200}, 120, 90, 0, 0, 360, 2.0), true);
    QCOMPARE(r.type, R::Ellipse);
    QVERIFY2(radiusSpread(r.path) < 1.0, "perfect ellipse should be a circle");
}

void KisQuickShapeRecognizerTest::testRectangle()
{
    QCOMPARE(R::recognize(polyline({{0, 0}, {300, 0}, {300, 180}, {0, 180}, {4, 6}}, 40, 2.5)).type, R::Rectangle);
}

void KisQuickShapeRecognizerTest::testPerfectRectangleIsSquare()
{
    const R::Result r = R::recognize(polyline({{0, 0}, {300, 0}, {300, 240}, {0, 240}, {3, 4}}, 40, 2.0), true);
    QCOMPARE(r.type, R::Rectangle);
    const QRectF b = r.path.boundingRect();
    QVERIFY2(qAbs(b.width() - b.height()) < 1.0, "perfect rectangle should be a square");
}

void KisQuickShapeRecognizerTest::testTriangle()
{
    QCOMPARE(R::recognize(polyline({{0, 200}, {150, 0}, {300, 200}, {5, 196}}, 40, 2.5)).type, R::Polygon);
}

void KisQuickShapeRecognizerTest::testZigZagPolyline()
{
    QCOMPARE(R::recognize(polyline({{0, 0}, {120, 100}, {240, 0}, {360, 100}}, 40, 2.0)).type, R::Polyline);
}

void KisQuickShapeRecognizerTest::testArc()
{
    QCOMPARE(R::recognize(ellipse({200, 200}, 150, 150, 0, 200, 340, 2.0)).type, R::Arc);
}

void KisQuickShapeRecognizerTest::testScribbleIsNotAShape()
{
    QVector<QPointF> scribble;
    for (int i = 0; i < 200; i++) {
        scribble << QPointF(i * 2.0, 60 * std::sin(i * 0.37) + 40 * std::sin(i * 0.11) + 25 * std::cos(i * 0.9));
    }
    QCOMPARE(R::recognize(scribble).type, R::None);
}

void KisQuickShapeRecognizerTest::testTinyDotIsNotAShape()
{
    QCOMPARE(R::recognize({{10, 10}, {10.5, 10}, {11, 10.4}, {10.6, 10.9}, {10.2, 10.3}}).type, R::None);
}

SIMPLE_TEST_MAIN(KisQuickShapeRecognizerTest)
