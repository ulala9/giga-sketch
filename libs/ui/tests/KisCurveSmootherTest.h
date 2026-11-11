/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KIS_CURVE_SMOOTHER_TEST_H
#define KIS_CURVE_SMOOTHER_TEST_H

#include <simpletest.h>

class KisCurveSmootherTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testTapProducesNoSegments();
    void testStraightLineIsContinuousAndStraight();
    void testCatchUpEndsExactlyAtPen();
    void testWithoutCatchUpStopsShort();
    void testRestingPenCatchesUp();
    void testSharpCornerDoesNotOvershoot();
    void testFastStrokeLagsLessThanSlow();
    void testSlowStrokeIsActuallySmoothed();
    void testTailGlidesInWithEaseOut();
    void testTailCanBeFinishedEarly();
    void testPendingPathEndsAtPen();
};

#endif
