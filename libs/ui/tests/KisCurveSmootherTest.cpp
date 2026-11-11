/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisCurveSmootherTest.h"

#include "tool/KisCurveSmoother.h"
#include <brushengine/kis_paint_information.h>

#include <QtMath>

namespace {

KisPaintInformation sample(const QPointF &pos, qreal timeMs, qreal pressure = 1.0)
{
    return KisPaintInformation(pos, pressure, 0, 0, 0, 0, 1.0, timeMs, 0);
}

KisCurveSmoother::Params params(qreal amount, qreal speedInfluence = 0.5)
{
    KisCurveSmoother::Params p;
    p.amount = amount;
    p.speedInfluence = speedInfluence;
    p.pressureInfluence = 0.0;
    p.zoom = 1.0;
    return p;
}

// Feeds a polyline sampled every `stepPx` pixels, one sample per `msPerSample`.
// Returns the time of the last sample.
qreal feedPolyline(KisCurveSmoother &s, const QVector<QPointF> &corners,
                   qreal stepPx, qreal msPerSample, qreal startTime,
                   QVector<KisCurveSmoother::Segment> &out)
{
    qreal t = startTime;
    for (int c = 1; c < corners.size(); c++) {
        const QPointF a = corners[c - 1];
        const QPointF b = corners[c];
        const qreal len = QLineF(a, b).length();
        const int n = qMax(1, qRound(len / stepPx));
        for (int i = 1; i <= n; i++) {
            t += msPerSample;
            s.addSample(sample(a + (b - a) * (qreal(i) / n), t), out);
        }
    }
    return t;
}

void verifyChained(const QVector<KisCurveSmoother::Segment> &segs)
{
    for (int i = 1; i < segs.size(); i++) {
        QCOMPARE(segs[i].p1.pos(), segs[i - 1].p2.pos());
    }
}

} // namespace

void KisCurveSmootherTest::testTapProducesNoSegments()
{
    KisCurveSmoother s;
    QVector<KisCurveSmoother::Segment> out;
    s.begin(sample(QPointF(50, 50), 0), params(0.5));
    s.end(true, out);
    // Nothing to stroke; the freehand helper paints a single dab instead.
    QVERIFY(out.isEmpty());
    QVERIFY(!s.isActive());
}

void KisCurveSmootherTest::testStraightLineIsContinuousAndStraight()
{
    KisCurveSmoother s;
    QVector<KisCurveSmoother::Segment> out;
    s.begin(sample(QPointF(0, 0), 0), params(0.5));
    feedPolyline(s, {QPointF(0, 0), QPointF(400, 0)}, 2.0, 5.0, 0, out);
    s.end(true, out);

    QVERIFY(out.size() > 10);
    verifyChained(out);
    for (const auto &seg : out) {
        QVERIFY(qAbs(seg.p1.pos().y()) < 1e-6);
        QVERIFY(qAbs(seg.control1.y()) < 1e-6);
        QVERIFY(qAbs(seg.control2.y()) < 1e-6);
        QVERIFY(qAbs(seg.p2.pos().y()) < 1e-6);
    }
    QCOMPARE(out.first().p1.pos(), QPointF(0, 0));
}

void KisCurveSmootherTest::testCatchUpEndsExactlyAtPen()
{
    KisCurveSmoother s;
    QVector<KisCurveSmoother::Segment> out;
    s.begin(sample(QPointF(0, 0), 0), params(0.9));
    feedPolyline(s, {QPointF(0, 0), QPointF(300, 100)}, 3.0, 5.0, 0, out);
    QVERIFY(s.lagInScreenPixels() > 5.0); // heavy smoothing trails the pen
    s.end(true, out);

    QVERIFY(!out.isEmpty());
    verifyChained(out);
    QCOMPARE(out.last().p2.pos(), QPointF(300, 100));
}

void KisCurveSmootherTest::testWithoutCatchUpStopsShort()
{
    KisCurveSmoother s;
    QVector<KisCurveSmoother::Segment> out;
    s.begin(sample(QPointF(0, 0), 0), params(0.9));
    feedPolyline(s, {QPointF(0, 0), QPointF(300, 0)}, 3.0, 5.0, 0, out);
    s.end(false, out);

    QVERIFY(!out.isEmpty());
    QVERIFY(out.last().p2.pos().x() < 295.0);
}

void KisCurveSmootherTest::testRestingPenCatchesUp()
{
    KisCurveSmoother s;
    QVector<KisCurveSmoother::Segment> out;
    s.begin(sample(QPointF(0, 0), 0), params(0.7));
    qreal t = feedPolyline(s, {QPointF(0, 0), QPointF(200, 0)}, 2.0, 5.0, 0, out);
    QVERIFY(s.lagInScreenPixels() > 1.0);

    int steps = 0;
    while (s.catchUpStep(int(t += 16), out) && steps < 200) {
        steps++;
    }
    QVERIFY(steps < 200);
    QVERIFY(s.lagInScreenPixels() <= 0.5);
    // Catching up should take a moment (an ease), not one jump.
    QVERIFY(steps >= 2);
}

void KisCurveSmootherTest::testSharpCornerDoesNotOvershoot()
{
    // A "V": down to an apex at y = 100 and back up. A curve that overshoots
    // would dip below the apex.
    KisCurveSmoother s;
    QVector<KisCurveSmoother::Segment> out;
    s.begin(sample(QPointF(0, 0), 0), params(0.0, 0.0));
    feedPolyline(s, {QPointF(0, 0), QPointF(100, 100), QPointF(200, 0)}, 4.0, 5.0, 0, out);
    s.end(true, out);

    verifyChained(out);
    qreal maxY = 0;
    for (const auto &seg : out) {
        maxY = qMax(maxY, qMax(seg.p1.pos().y(), seg.p2.pos().y()));
        maxY = qMax(maxY, qMax(seg.control1.y(), seg.control2.y()));
    }
    QVERIFY2(maxY <= 100.5, qPrintable(QString("max y = %1").arg(maxY)));
}

void KisCurveSmootherTest::testFastStrokeLagsLessThanSlow()
{
    // Same line drawn slowly and quickly. With speed influence, the fast
    // stroke should trail the pen by less *time* than the slow one.
    auto timeLag = [](qreal msPerSample) {
        KisCurveSmoother s;
        QVector<KisCurveSmoother::Segment> out;
        s.begin(sample(QPointF(0, 0), 0), params(0.6, 1.0));
        feedPolyline(s, {QPointF(0, 0), QPointF(600, 0)}, 3.0, msPerSample, 0, out);
        const qreal speedPxPerMs = 3.0 / msPerSample;
        return s.lagInScreenPixels() / speedPxPerMs; // ms behind the pen
    };
    const qreal slow = timeLag(20.0); // 150 px/s
    const qreal fast = timeLag(2.0);  // 1500 px/s
    QVERIFY2(fast < slow, qPrintable(QString("fast %1 ms, slow %2 ms").arg(fast).arg(slow)));
}

void KisCurveSmootherTest::testSlowStrokeIsActuallySmoothed()
{
    // A slow, shaky horizontal line (+-3 px zig-zag). The smoothed line
    // should wobble much less than the input. This guards against the
    // filter treating its own lag as "speed" and switching itself off.
    KisCurveSmoother s;
    QVector<KisCurveSmoother::Segment> out;
    s.begin(sample(QPointF(0, 0), 0), params(0.5, 0.5));
    qreal t = 0;
    for (int i = 1; i <= 200; i++) {
        t += 8.0; // ~ 250 px/s
        s.addSample(sample(QPointF(i * 2.0, (i % 2) ? 3.0 : -3.0), t), out);
    }
    QVERIFY(out.size() > 10);
    qreal maxDev = 0;
    for (int i = out.size() / 3; i < out.size(); i++) { // skip the start-up
        maxDev = qMax(maxDev, qAbs(out[i].p2.pos().y()));
    }
    QVERIFY2(maxDev < 1.0, qPrintable(QString("max wobble %1 px").arg(maxDev)));
}

void KisCurveSmootherTest::testTailGlidesInWithEaseOut()
{
    KisCurveSmoother s;
    QVector<KisCurveSmoother::Segment> out;
    s.begin(sample(QPointF(0, 0), 0), params(0.9));
    qreal t = feedPolyline(s, {QPointF(0, 0), QPointF(400, 0)}, 4.0, 4.0, 0, out);
    const qreal startLag = s.lagInScreenPixels();
    QVERIFY(startLag > 20.0);

    out.clear();
    s.startTail(int(t), 120);
    QVERIFY(s.hasTail());

    // Track how far the head moves each 8 ms frame.
    QVector<qreal> stepLengths;
    qreal prevX = s.lastRawInfo().pos().x() - startLag;
    bool landed = false;
    int frames = 0;
    while (!landed && frames < 100) {
        t += 8;
        landed = s.tailStep(int(t), out);
        frames++;
        const qreal x = landed ? 400.0 : (s.lastRawInfo().pos().x() - s.lagInScreenPixels());
        stepLengths << (x - prevX);
        prevX = x;
    }
    QVERIFY(landed);
    QVERIFY(frames <= 120 / 8 + 1);          // finished on time
    QCOMPARE(out.last().p2.pos(), QPointF(400, 0)); // landed on the pen
    verifyChained(out);
    // Ease-out: early frames move further than late ones.
    QVERIFY(stepLengths.first() > stepLengths[stepLengths.size() - 2]);
    QVERIFY(!s.isActive());
}

void KisCurveSmootherTest::testTailCanBeFinishedEarly()
{
    KisCurveSmoother s;
    QVector<KisCurveSmoother::Segment> out;
    s.begin(sample(QPointF(0, 0), 0), params(0.9));
    qreal t = feedPolyline(s, {QPointF(0, 0), QPointF(0, 300)}, 4.0, 4.0, 0, out);
    s.startTail(int(t), 200);
    s.tailStep(int(t + 16), out);
    // New stroke / undo / tool switch: finish now.
    s.end(false, out); // even with catchUp=false, a running tail lands on the pen
    QCOMPARE(out.last().p2.pos(), QPointF(0, 300));
    QVERIFY(!s.isActive());
    QVERIFY(!s.hasTail());
}

void KisCurveSmootherTest::testPendingPathEndsAtPen()
{
    KisCurveSmoother s;
    QVector<KisCurveSmoother::Segment> out;
    s.begin(sample(QPointF(0, 0), 0), params(0.8));
    feedPolyline(s, {QPointF(0, 0), QPointF(200, 50)}, 3.0, 5.0, 0, out);
    const QPolygonF path = s.pendingPath();
    QVERIFY(path.size() >= 2);
    QCOMPARE(path.first(), out.last().p2.pos()); // starts where paint ended
    QCOMPARE(path.last(), QPointF(200, 50));     // ends at the pen
}

SIMPLE_TEST_MAIN(KisCurveSmootherTest)
