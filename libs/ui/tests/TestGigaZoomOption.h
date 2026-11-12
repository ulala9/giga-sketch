/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef TEST_GIGA_ZOOM_OPTION_H
#define TEST_GIGA_ZOOM_OPTION_H

#include <QObject>

class TestGigaZoomOption : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testSpaceDragZoomSwitches();
    void testOtherBindingsUntouched();
};

#endif
