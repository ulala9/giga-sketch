/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KIS_QUICK_SHAPE_RECOGNIZER_TEST_H
#define KIS_QUICK_SHAPE_RECOGNIZER_TEST_H

#include <simpletest.h>

class KisQuickShapeRecognizerTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testWobblyLine();
    void testPerfectLineSnapsAngle();
    void testCircle();
    void testRotatedEllipse();
    void testPerfectEllipseIsCircle();
    void testRectangle();
    void testPerfectRectangleIsSquare();
    void testTriangle();
    void testZigZagPolyline();
    void testArc();
    void testScribbleIsNotAShape();
    void testTinyDotIsNotAShape();
};

#endif
