/*
 *  SPDX-FileCopyrightText: 2026 The Giga Sketch contributors
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef TEST_GIGA_REFERENCE_LAYOUT_H
#define TEST_GIGA_REFERENCE_LAYOUT_H

#include <QObject>

class TestGigaReferenceLayout : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testNormalize();
    void testStylesKeepAspectAndDoNotOverlap_data();
    void testStylesKeepAspectAndDoNotOverlap();
    void testJustifiedRowsShareRightEdge();
    void testPackedKeepsSizes();
    void testAutoPicksAStyle();
    void testReadingOrder();
    void testEmptyAndInvalid();
};

#endif
