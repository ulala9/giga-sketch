/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "TestGigaCanvasInputKeys.h"

#include <GigaCanvasInputKeys.h>
#include <simpletest.h>

using namespace GigaCanvasInputKeys;

namespace {
Kind kindAt(const QVector<Binding> &b, int i)
{
    return i < 0 ? Kind::RotateReset : b[i].kind;
}
}

void TestGigaCanvasInputKeys::testDragMatchIsExact()
{
    const QVector<Binding> b = defaultBindings();
    const QSet<int> space{Qt::Key_Space};

    int i = matchDrag(b, heldKeys(Qt::ControlModifier, space), Qt::LeftButton);
    QVERIFY(i >= 0);
    QCOMPARE(int(kindAt(b, i)), int(Kind::Zoom));
    QVERIFY(b[i].towardPointer);

    i = matchDrag(b, heldKeys(Qt::ControlModifier | Qt::AltModifier, space), Qt::LeftButton);
    QCOMPARE(int(kindAt(b, i)), int(Kind::ZoomDiscrete));

    i = matchDrag(b, heldKeys(Qt::ShiftModifier, space), Qt::LeftButton);
    QCOMPARE(int(kindAt(b, i)), int(Kind::Rotate));

    i = matchDrag(b, heldKeys(Qt::ShiftModifier | Qt::AltModifier, space), Qt::LeftButton);
    QCOMPARE(int(kindAt(b, i)), int(Kind::RotateDiscrete));

    // Plain Space (pan), Ctrl alone (colour pick), wrong button: no match.
    QCOMPARE(matchDrag(b, heldKeys(Qt::NoModifier, space), Qt::LeftButton), -1);
    QCOMPARE(matchDrag(b, heldKeys(Qt::ControlModifier, {}), Qt::LeftButton), -1);
    QCOMPARE(matchDrag(b, heldKeys(Qt::ControlModifier, space), Qt::RightButton), -1);
    // Extra key held: not an exact match.
    QCOMPARE(matchDrag(b, heldKeys(Qt::ControlModifier | Qt::ShiftModifier, space), Qt::LeftButton), -1);
}

void TestGigaCanvasInputKeys::testResetKey()
{
    const QVector<Binding> b = defaultBindings();
    const int i = matchKey(b, heldKeys(Qt::NoModifier, {Qt::Key_5}));
    QVERIFY(i >= 0);
    QCOMPARE(int(b[i].kind), int(Kind::RotateReset));
    QCOMPARE(matchKey(b, heldKeys(Qt::ControlModifier, {Qt::Key_5})), -1);
    // Drag bindings never match as plain keys.
    QCOMPARE(matchKey(b, heldKeys(Qt::ShiftModifier, {Qt::Key_Space})), -1);
}

void TestGigaCanvasInputKeys::testZoomDragSteps()
{
    const QPointF start(100, 100);
    // Up = zoom in, down = out.
    QCOMPARE(zoomDragSteps(start, QPointF(100, 40), false, false), 60.0);
    QCOMPARE(zoomDragSteps(start, QPointF(100, 130), false, false), -30.0);
    // Horizontal: right = in.
    QCOMPARE(zoomDragSteps(start, QPointF(150, 0), true, false), 50.0);
    // Inverted flips it.
    QCOMPARE(zoomDragSteps(start, QPointF(100, 40), false, true), -60.0);
}

SIMPLE_TEST_MAIN(TestGigaCanvasInputKeys)
