/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef TEST_GIGA_CANVAS_INPUT_KEYS_H
#define TEST_GIGA_CANVAS_INPUT_KEYS_H

#include <QObject>

class TestGigaCanvasInputKeys : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testDragMatchIsExact();
    void testResetKey();
    void testZoomDragSteps();
};

#endif
