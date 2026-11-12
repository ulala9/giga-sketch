/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "TestGigaZoomOption.h"

#include <GigaZoomOption.h>
#include <input/kis_zoom_action.h>
#include <simpletest.h>

namespace {
const QList<Qt::Key> CTRL_SPACE = {Qt::Key_Control, Qt::Key_Space};
const QList<Qt::Key> CTRL_ALT_SPACE = {Qt::Key_Control, Qt::Key_Alt, Qt::Key_Space};
}

void TestGigaZoomOption::testSpaceDragZoomSwitches()
{
    using GigaZoomOption::modeFor;
    QCOMPARE(modeFor(KisZoomAction::ZoomModeShortcut, CTRL_SPACE, true), uint(KisZoomAction::RelativeZoomModeShortcut));
    QCOMPARE(modeFor(KisZoomAction::RelativeZoomModeShortcut, CTRL_SPACE, false), uint(KisZoomAction::ZoomModeShortcut));
    QCOMPARE(modeFor(KisZoomAction::DiscreteZoomModeShortcut, CTRL_ALT_SPACE, true), uint(KisZoomAction::RelativeDiscreteZoomModeShortcut));
    QCOMPARE(modeFor(KisZoomAction::RelativeDiscreteZoomModeShortcut, CTRL_ALT_SPACE, false), uint(KisZoomAction::DiscreteZoomModeShortcut));
    // Already right: unchanged.
    QCOMPARE(modeFor(KisZoomAction::RelativeZoomModeShortcut, CTRL_SPACE, true), uint(KisZoomAction::RelativeZoomModeShortcut));
}

void TestGigaZoomOption::testOtherBindingsUntouched()
{
    using GigaZoomOption::modeFor;
    // Not a Space binding (e.g. Ctrl + middle-drag): left alone.
    QCOMPARE(modeFor(KisZoomAction::ZoomModeShortcut, {Qt::Key_Control}, true), uint(KisZoomAction::ZoomModeShortcut));
    // Space binding but not a drag zoom (e.g. zoom in key): left alone.
    QCOMPARE(modeFor(KisZoomAction::ZoomInShortcut, CTRL_SPACE, true), uint(KisZoomAction::ZoomInShortcut));
    QCOMPARE(modeFor(KisZoomAction::Zoom100PctShortcut, CTRL_SPACE, false), uint(KisZoomAction::Zoom100PctShortcut));
}

SIMPLE_TEST_MAIN(TestGigaZoomOption)
